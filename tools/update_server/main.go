package main

import (
	"bufio"
	"crypto/sha1"
	"encoding/hex"
	"encoding/json"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

type fileInfo struct {
	Name string `json:"name"`
	URL  string `json:"url"`
	Size int64  `json:"size"`
	SHA1 string `json:"sha1"`
}

type updateCheckRequest struct {
	CurrentVersion string `json:"currentVersion"`
	Platform       string `json:"platform"`
}

type updateResponse struct {
	HasUpdate     bool       `json:"hasUpdate"`
	LatestVersion string     `json:"latestVersion"`
	BasePackage   string     `json:"basePackage"`
	FullFile      *fileInfo  `json:"fullFile,omitempty"`
	DeltaFiles    []fileInfo `json:"deltaFiles,omitempty"`
}

type releaseEntry struct {
	Version  string
	FileName string
	IsFull   bool
	Size     int64
}

type versionInfo struct {
	Version string
	Full    *releaseEntry
	Delta   *releaseEntry
}

func main() {
	addr := "0.0.0.0:9000"
	baseURL := os.Getenv("CP_UPDATE_BASE_URL")
	if baseURL == "" {
		baseURL = "http://localhost:9000"
	}

	releasesDir := "./releases"

	log.Printf("Server starting with baseURL: %s", baseURL)
	log.Printf("Releases directory: %s", releasesDir)
	log.Printf("Server will listen on %s", addr)

	http.Handle("/releases/", http.StripPrefix("/releases/", http.FileServer(http.Dir(releasesDir))))
	http.HandleFunc("/api/update/check", func(w http.ResponseWriter, r *http.Request) {
		handleUpdateCheck(w, r, baseURL, releasesDir)
	})

	log.Println("update server listening on", addr)
	if err := http.ListenAndServe(addr, nil); err != nil {
		log.Fatal(err)
	}
}

func handleUpdateCheck(w http.ResponseWriter, r *http.Request, baseURL, releasesDir string) {
	// 添加CORS头
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

	if r.Method != http.MethodGet && r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}

	var currentVersion string
	var platform string

	if r.Method == http.MethodPost {
		var req updateCheckRequest
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, "invalid json body", http.StatusBadRequest)
			return
		}
		currentVersion = strings.TrimSpace(req.CurrentVersion)
		platform = req.Platform
	} else {
		currentVersion = strings.TrimSpace(r.URL.Query().Get("currentVersion"))
		platform = r.URL.Query().Get("platform")
	}

	if currentVersion == "" {
		http.Error(w, "currentVersion is required", http.StatusBadRequest)
		return
	}

	// 使用请求的Host头构建正确的baseURL
	scheme := "http"
	if r.TLS != nil {
		scheme = "https"
	}
	requestBaseURL := scheme + "://" + r.Host

	log.Printf("update check: currentVersion=%s platform=%s", currentVersion, platform)

	entries, err := parseReleases(filepath.Join(releasesDir, "RELEASES"))
	if err != nil || len(entries) == 0 {
		resp := updateResponse{
			HasUpdate:     false,
			LatestVersion: currentVersion,
			BasePackage:   "",
		}
		writeJSON(w, http.StatusOK, resp)
		return
	}

	infos := buildVersionInfos(entries)
	if len(infos) == 0 {
		resp := updateResponse{
			HasUpdate:     false,
			LatestVersion: currentVersion,
			BasePackage:   "",
		}
		writeJSON(w, http.StatusOK, resp)
		return
	}

	sort.Slice(infos, func(i, j int) bool {
		return versionGreater(infos[i].Version, infos[j].Version) == false
	})

	// 查找 currentVersion 对应版本的 full 包文件名，用于 BasePackage 返回给客户端。
	baseFullName := ""
	for _, info := range infos {
		if info.Version == currentVersion && info.Full != nil {
			baseFullName = info.Full.FileName
			break
		}
	}

	var chain []versionInfo
	for _, info := range infos {
		if versionGreater(info.Version, currentVersion) {
			chain = append(chain, info)
		}
	}

	if len(chain) == 0 {
		resp := updateResponse{
			HasUpdate:     false,
			LatestVersion: currentVersion,
			BasePackage:   "",
		}
		writeJSON(w, http.StatusOK, resp)
		return
	}

	target := chain[len(chain)-1]
	latestVersion := target.Version

	resp := updateResponse{
		LatestVersion: latestVersion,
		BasePackage:   baseFullName,
	}

	var fullInfo *fileInfo
	if target.Full != nil {
		fi, err := buildFileInfo(releasesDir, target.Full.FileName, requestBaseURL)
		if err != nil {
			log.Printf("error building full file info: %v", err)
			http.Error(w, "full file not available", http.StatusInternalServerError)
			return
		}
		fullInfo = fi
	}

	var deltaFiles []fileInfo
	for _, info := range chain {
		if info.Delta == nil {
			continue
		}
		df, err := buildFileInfo(releasesDir, info.Delta.FileName, requestBaseURL)
		if err != nil {
			log.Printf("error building delta file info: %v", err)
			continue
		}
		deltaFiles = append(deltaFiles, *df)
	}

	// 只要存在比 currentVersion 新的版本，就认为有更新。
	if fullInfo == nil && len(deltaFiles) == 0 {
		resp.HasUpdate = false
		resp.LatestVersion = currentVersion
		resp.BasePackage = ""
		writeJSON(w, http.StatusOK, resp)
		return
	}

	resp.HasUpdate = true
	if fullInfo != nil {
		resp.FullFile = fullInfo
	}
	if len(deltaFiles) > 0 {
		resp.DeltaFiles = deltaFiles
	}
	writeJSON(w, http.StatusOK, resp)
}

func buildFileInfo(releasesDir, name, baseURL string) (*fileInfo, error) {
	path := filepath.Join(releasesDir, name)
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	info, err := f.Stat()
	if err != nil {
		return nil, err
	}

	h := sha1.New()
	if _, err := io.Copy(h, f); err != nil {
		return nil, err
	}
	sum := hex.EncodeToString(h.Sum(nil))

	url := baseURL + "/releases/" + name

	return &fileInfo{
		Name: name,
		URL:  url,
		Size: info.Size(),
		SHA1: sum,
	}, nil
}

func writeJSON(w http.ResponseWriter, status int, v interface{}) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	enc := json.NewEncoder(w)
	_ = enc.Encode(v)
}

func parseReleases(path string) ([]releaseEntry, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var res []releaseEntry
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		parts := strings.Fields(line)
		if len(parts) < 3 {
			continue
		}
		var filename string
		for _, p := range parts {
			if strings.HasSuffix(strings.ToLower(p), ".nupkg") {
				filename = p
				break
			}
		}
		if filename == "" {
			continue
		}
		sizeStr := parts[len(parts)-1]
		size, err := strconv.ParseInt(sizeStr, 10, 64)
		if err != nil {
			continue
		}
		version := extractVersionFromFileName(filename)
		if version == "" {
			continue
		}
		lower := strings.ToLower(filename)
		isFull := strings.Contains(lower, "-full")
		res = append(res, releaseEntry{
			Version:  version,
			FileName: filename,
			IsFull:   isFull,
			Size:     size,
		})
	}
	return res, scanner.Err()
}

func buildVersionInfos(entries []releaseEntry) []versionInfo {
	m := make(map[string]*versionInfo)
	for i := range entries {
		e := &entries[i]
		v := e.Version
		info, ok := m[v]
		if !ok {
			info = &versionInfo{Version: v}
			m[v] = info
		}
		lower := strings.ToLower(e.FileName)
		if strings.Contains(lower, "-full") {
			info.Full = e
		} else if strings.Contains(lower, "-delta") {
			info.Delta = e
		}
	}
	var res []versionInfo
	for _, v := range m {
		res = append(res, *v)
	}
	return res
}

func extractVersionFromFileName(name string) string {
	base := filepath.Base(name)
	base = strings.TrimSuffix(base, ".nupkg")

	lower := strings.ToLower(base)
	if strings.HasSuffix(lower, "-full") {
		base = base[:len(base)-len("-full")]
		lower = lower[:len(lower)-len("-full")]
	} else if strings.HasSuffix(lower, "-delta") {
		base = base[:len(base)-len("-delta")]
		lower = lower[:len(lower)-len("-delta")]
	}

	start := -1
	for i, ch := range base {
		if ch >= '0' && ch <= '9' {
			start = i
			break
		}
	}
	if start == -1 || start >= len(base) {
		return ""
	}
	return base[start:]
}

func versionGreater(a, b string) bool {
	am, an, ap, aextra := splitVersion(a)
	bm, bn, bp, bextra := splitVersion(b)
	if am != bm {
		return am > bm
	}
	if an != bn {
		return an > bn
	}
	if ap != bp {
		return ap > bp
	}
	if aextra == bextra {
		return false
	}
	if aextra == "" {
		return true
	}
	if bextra == "" {
		return false
	}
	return aextra > bextra
}

func splitVersion(v string) (int, int, int, string) {
	v = strings.TrimSpace(v)
	if v == "" {
		return 0, 0, 0, ""
	}
	body := v
	extra := ""
	if i := strings.Index(body, "-"); i >= 0 {
		extra = body[i+1:]
		body = body[:i]
	}
	parts := strings.Split(body, ".")
	parse := func(s string) int {
		if s == "" {
			return 0
		}
		n, err := strconv.Atoi(s)
		if err != nil {
			return 0
		}
		return n
	}
	maj := 0
	min := 0
	pat := 0
	if len(parts) > 0 {
		maj = parse(parts[0])
	}
	if len(parts) > 1 {
		min = parse(parts[1])
	}
	if len(parts) > 2 {
		pat = parse(parts[2])
	}
	return maj, min, pat, extra
}
