import time
import autoit
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

finish = False
class LogFileHandler(FileSystemEventHandler):
    def __init__(self, log_file_path, keyword, callback):
        self.keyword = keyword
        self.callback = callback
        self.log_file_path = log_file_path
        self.last_position = 0 

    def on_modified(self, event):
        if event.src_path.endswith(".tlog"):
            self.check_log_for_keyword(event.src_path)

    def check_log_for_keyword(self, log_file_path):
        try:
            with open(log_file_path, "r") as f:
                # f.seek(self.last_position)
                new_lines = f.readlines()
                self.last_position = f.tell()

            for line in new_lines:
                if self.keyword in line:
                    print(f"found keyword '{self.keyword}' in log: {line.strip()}")
                    self.callback()
                    break
        except Exception as e:
            print(f"raed log error: {e}")


def run(log_dir, log_file_path, keyword, operation):
    global finish
    finish = False
    
    def on_keyword_found():
        global finish
        finish = True

    # enable log listen
    event_handler = LogFileHandler(log_file_path, keyword, on_keyword_found)
    observer = Observer()
    observer.schedule(event_handler, path=log_dir, recursive=False)  # 监控当前目录
    observer.start()

    ################## operation ##############
    operation()
    ###########################################

    try:
        print("wait respone...")
        while finish == False:
            time.sleep(0.3)
        print("finish")
    except KeyboardInterrupt:
        pass
    observer.stop()
    observer.join()
