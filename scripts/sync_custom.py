import os
import sys
import shutil
import subprocess
from pathlib import Path

def run(cmd, cwd=None):
    print(f"Running: {cmd}")
    result = subprocess.run(cmd, shell=True, cwd=cwd)
    if result.returncode != 0:
        print(f"Command failed: {cmd}")
        sys.exit(result.returncode)

def main():
    # 参数处理
    web_branch = sys.argv[1] if len(sys.argv) > 1 else "master"
    if len(sys.argv) > 2:
        web_root = Path(sys.argv[2]).resolve()
    else:
        web_root = (Path(__file__).parent / "../C3DSlicerCustom").resolve()

    cp_source = Path.cwd()
    print(f"web_root={web_root}")
    print(f"cp_source={cp_source}")

    if not web_root.exists():
        print(f"{web_root} not exist")
        sys.exit(1)

    sync_source_dir = web_root / "customized"
    sync_target_dir = cp_source / "customized"

    # WEB_REPO
    print("WEB_REPO start")
    print(f"curwebdir={web_root}")

    run("git fetch", cwd=web_root)
    run(f"git checkout {web_branch}", cwd=web_root)
    run(f"git pull origin {web_branch}", cwd=web_root)

    # SYNC_WEB
    if sync_target_dir.exists():
        print(f"{sync_target_dir} is exist, delete it")
        shutil.rmtree(sync_target_dir)

    if not sync_source_dir.exists():
        print(f"{sync_source_dir} not exist")
        sys.exit(1)

    print("copy start")
    shutil.copytree(sync_source_dir, sync_target_dir)
    print("copy end")

    # END
    os.chdir(cp_source)
    print("web sync end")

if __name__ == "__main__":
    main()