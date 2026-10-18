import argparse
import json
import logging
import subprocess
import sys
import traceback
from pathlib import Path


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--project-root")
    p.add_argument("--queue-dir")
    p.add_argument("--compile-db")
    p.add_argument("--clang-tidy-bin")
    p.add_argument("--output-json")
    p.add_argument("--no-diff-filter", action="store_true")
    p.add_argument("--line-context", type=int, default=3)
    return p.parse_args()


def setup_logger(log_path: Path):
    log_path.parent.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s [%(levelname)s] %(message)s",
        handlers=[
            logging.FileHandler(log_path, encoding="utf-8"),
            logging.StreamHandler(sys.stdout),
        ],
    )


def load_changed_files(queue_dir: Path) -> list[dict]:
    jf = queue_dir / "changed_files.json"
    if not jf.exists():
        logging.warning("changed_files.json 不存在: %s", jf)
        return []
    try:
        text = jf.read_text(encoding="utf-8")
        data = json.loads(text)
        logging.info("读取 changed_files.json 成功: %s 字节", len(text))
    except Exception:
        logging.error("解析 changed_files.json 失败: %s", jf)
        logging.debug(traceback.format_exc())
        return []
    files = data.get("files")
    if not isinstance(files, list):
        logging.warning("changed_files.json 中 files 字段不是列表")
        return []
    out = []
    for item in files:
        if not isinstance(item, dict):
            continue
        path = item.get("path")
        status = item.get("status") or ""
        if not path:
            continue
        out.append({"path": str(path), "status": str(status)})
    logging.info("解析变更文件 %d 个", len(out))
    return out


def parse_diff_hunks(diff_path: Path) -> dict[str, list[tuple[int, int]]]:
    if not diff_path.exists():
        logging.warning("patch.diff 不存在: %s", diff_path)
        return {}
    text = diff_path.read_text(encoding="utf-8", errors="replace")
    logging.info("读取 patch.diff 成功: %d 字节", len(text))
    result: dict[str, list[tuple[int, int]]] = {}
    cur_file = None
    for line in text.splitlines():
        if line.startswith("+++ "):
            name = line[4:].strip()
            if name.startswith("b/"):
                name = name[2:]
            cur_file = name
            if cur_file not in result:
                result[cur_file] = []
            continue
        if not line.startswith("@@"):
            continue
        parts = line.split("@@")
        if len(parts) < 3:
            continue
        seg = parts[1].strip()
        try:
            seg_parts = seg.split()
            if len(seg_parts) < 2:
                continue
            new_info = seg_parts[1]
            if not new_info.startswith("+"):
                continue
            new_info = new_info[1:]
            if "," in new_info:
                start_s, count_s = new_info.split(",", 1)
                start = int(start_s)
                count = int(count_s)
            else:
                start = int(new_info)
                count = 1
            if cur_file is None:
                continue
            rng = (start, start + max(count, 1) - 1)
            result.setdefault(cur_file, []).append(rng)
        except Exception:
            logging.debug("解析 hunk 行失败: %s", line)
            logging.debug(traceback.format_exc())
            continue
    logging.info("diff 中解析到有行范围的文件 %d 个", len(result))
    return result


def run_clang_tidy_for_file(
    bin_path: Path, compile_db_dir: Path, source_path: Path
) -> tuple[int, str, str]:
    cmd = [
        str(bin_path),
        "-p",
        str(compile_db_dir),
        str(source_path),
    ]
    logging.info("执行 clang-tidy: cmd=%s", " ".join(cmd))
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            cwd=str(compile_db_dir),
        )
        logging.info(
            "clang-tidy 结束: file=%s code=%s stdout_len=%d stderr_len=%d",
            source_path,
            proc.returncode,
            len(proc.stdout or ""),
            len(proc.stderr or ""),
        )
        return proc.returncode, proc.stdout, proc.stderr
    except FileNotFoundError:
        msg = f"clang-tidy 可执行文件不存在: {bin_path}"
        logging.error(msg)
        return 127, "", msg
    except Exception as e:
        logging.error("clang-tidy 运行异常: %s", e)
        logging.debug(traceback.format_exc())
        return 1, "", f"clang-tidy run failed: {e}"


def parse_clang_tidy_output(text: str, rel_file: str) -> list[dict]:
    issues: list[dict] = []
    rel_norm = rel_file.replace("\\", "/")
    for line in text.splitlines():
        lower = line.lower()
        if "pch file" in lower and "not found" in lower:
            issues.append(
                {
                    "file": rel_norm,
                    "line": 1,
                    "column": 1,
                    "severity": "error",
                    "check": "clang-diagnostic-error",
                    "message": line.strip(),
                }
            )
            continue
        parts = line.rsplit(":", 4)
        if len(parts) < 5:
            continue
        path = parts[0].strip()
        try:
            line_no = int(parts[1])
            col_no = int(parts[2])
        except ValueError:
            continue
        msg = parts[4].strip()
        sev = ""
        check = ""
        if "]" in msg:
            try:
                before, after = msg.split("]", 1)
                if "[" in before:
                    sev_part, chk_part = before.split("[", 1)
                    sev = sev_part.strip()
                    check = chk_part.strip()
                msg = after.strip()
            except ValueError:
                pass
        path_norm = path.replace("\\", "/")
        if not path_norm.endswith(rel_norm):
            continue
        issues.append(
            {
                "file": rel_norm,
                "line": line_no,
                "column": col_no,
                "severity": sev or "warning",
                "check": check or "",
                "message": msg,
            }
        )
    logging.info("解析 clang-tidy 输出: file=%s issues=%d", rel_file, len(issues))
    return issues


def expand_ranges(ranges: list[tuple[int, int]], ctx: int) -> list[tuple[int, int]]:
    out: list[tuple[int, int]] = []
    for s, e in ranges:
        s2 = max(1, s - ctx)
        e2 = e + ctx
        out.append((s2, e2))
    return out


def mark_in_diff(issues: list[dict], ranges: list[tuple[int, int]]) -> None:
    for item in issues:
        line = int(item.get("line") or 0)
        flag = False
        for s, e in ranges:
            if s <= line <= e:
                flag = True
                break
        item["in_diff"] = flag


def main():
    args = parse_args()
    script_dir = Path(__file__).resolve().parent
    default_project_root = script_dir.parent
    project_root = Path(args.project_root).resolve() if args.project_root else default_project_root
    default_queue_dir = project_root / "AICodeCheck" / "queue"
    queue_dir = Path(args.queue_dir).resolve() if args.queue_dir else default_queue_dir
    log_path = queue_dir / "clang_tidy_runner.log"
    setup_logger(log_path)
    logging.info("脚本启动")
    logging.info("script_dir=%s", script_dir)
    logging.info("project_root=%s (default=%s)", project_root, default_project_root)
    logging.info("queue_dir=%s (default=%s)", queue_dir, default_queue_dir)
    logging.info(
        "参数: compile_db=%s clang_tidy_bin=%s output_json=%s no_diff_filter=%s line_context=%s",
        args.compile_db,
        args.clang_tidy_bin,
        args.output_json,
        args.no_diff_filter,
        args.line_context,
    )
    compile_db = (
        Path(args.compile_db).resolve()
        if args.compile_db
        else project_root / "clang-tidy" / "compile_commands.json"
    )
    clang_tidy_bin = (
        Path(args.clang_tidy_bin).resolve()
        if args.clang_tidy_bin
        else project_root / "clang-tidy" / "clang-tidy.exe"
    )
    output_json = (
        Path(args.output_json).resolve()
        if args.output_json
        else queue_dir / "clang_tidy_result.json"
    )
    compile_db_dir = compile_db.parent
    logging.info("compile_commands.json=%s exists=%s", compile_db, compile_db.exists())
    logging.info("clang-tidy bin=%s exists=%s", clang_tidy_bin, clang_tidy_bin.exists())
    logging.info("output_json=%s", output_json)
    changed_files = load_changed_files(queue_dir)
    if not changed_files:
        logging.warning("没有可用的变更文件，输出空结果")
        data = {
            "version": 1,
            "tool": "clang-tidy",
            "summary": {"total_issues": 0, "error_count": 0, "warning_count": 0},
            "issues": [],
        }
        output_json.parent.mkdir(parents=True, exist_ok=True)
        output_json.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
        flag_path = queue_dir / "clang_finished.txt"
        flag_path.write_text("0", encoding="utf-8")
        logging.info("写入空结果到 %s 并写入 clang_finished.txt=0", output_json)
        logging.info("脚本结束")
        return 0
    diff_path = queue_dir / "patch.diff"
    diff_ranges = parse_diff_hunks(diff_path) if diff_path.exists() else {}
    logging.info("diff_ranges 文件数=%d", len(diff_ranges))
    issues_all: list[dict] = []
    for item in changed_files:
        rel_path = str(item["path"]).replace("\\", "/")
        status = item.get("status") or ""
        logging.info("处理文件: path=%s status=%s", rel_path, status)
        if status.startswith("D"):
            logging.info("文件已删除，跳过: %s", rel_path)
            continue
        src_path = project_root / rel_path
        logging.info("解析后的源码路径: %s exists=%s", src_path, src_path.exists())
        if not src_path.exists():
            continue
        if not any(
            src_path.suffix.lower().endswith(ext) for ext in (".c", ".cc", ".cpp", ".cxx", ".mm")
        ):
            logging.info("非 C/C++ 文件，跳过: %s", src_path)
            continue
        code, out_text, err_text = run_clang_tidy_for_file(
            clang_tidy_bin, compile_db_dir, src_path
        )
        if code != 0 and err_text:
            logging.warning(
                "clang-tidy 返回非零: file=%s code=%s stderr前200=%s",
                rel_path,
                code,
                err_text[:200],
            )
        combined_output = out_text or ""
        if err_text:
            combined_output = (combined_output + "\n" + err_text) if combined_output else err_text
        logging.info(
            "clang-tidy 原始输出长度: stdout=%d stderr=%d combined=%d",
            len(out_text or ""),
            len(err_text or ""),
            len(combined_output),
        )
        try:
            preview_len = min(len(combined_output), 4000)
            if preview_len > 0:
                logging.debug(
                    "clang-tidy 原始输出预览前%d字节:\n%s",
                    preview_len,
                    combined_output[:preview_len],
                )
        except Exception:
            pass
        file_issues = parse_clang_tidy_output(combined_output, rel_path)
        key = rel_path.replace("\\", "/")
        ranges = diff_ranges.get(key) or []
        logging.info("文件 diff 范围: key=%s ranges=%s", key, ranges if ranges else "[]")
        if ranges and not args.no_diff_filter:
            ctx = max(0, int(args.line_context))
            ranges_expanded = expand_ranges(ranges, ctx)
            logging.info("应用行级过滤: ctx=%d expanded_ranges=%s", ctx, ranges_expanded)
            mark_in_diff(file_issues, ranges_expanded)
        else:
            default_flag = True if args.no_diff_filter else False
            logging.info(
                "不按 diff 过滤 in_diff，全部设置为: %s (no_diff_filter=%s ranges_exist=%s)",
                default_flag,
                args.no_diff_filter,
                bool(ranges),
            )
            for obj in file_issues:
                obj["in_diff"] = default_flag
        issues_all.extend(file_issues)
    err_count = sum(
        1 for it in issues_all if (it.get("severity") or "").lower() in ("error", "fatal")
    )
    warn_count = sum(
        1 for it in issues_all if (it.get("severity") or "").lower() not in ("error", "fatal")
    )
    diff_issue_count = sum(1 for it in issues_all if it.get("in_diff"))
    flag_val = "1" if diff_issue_count > 0 else "0"
    data = {
        "version": 1,
        "tool": "clang-tidy",
        "summary": {
            "total_issues": len(issues_all),
            "error_count": err_count,
            "warning_count": warn_count,
            "diff_issues": diff_issue_count,
        },
        "issues": issues_all,
    }
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    flag_path = queue_dir / "clang_finished.txt"
    flag_path.write_text(flag_val, encoding="utf-8")
    logging.info(
        "写入结果到 %s: total=%d error=%d warning=%d diff_issues=%d",
        output_json,
        len(issues_all),
        err_count,
        warn_count,
        diff_issue_count,
    )
    logging.info("写入 clang_finished.txt=%s 路径=%s", flag_val, flag_path)
    logging.info("脚本结束")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
