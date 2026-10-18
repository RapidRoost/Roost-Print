import time
import pyautogui
import pytest
import pyperclip
import os

# 关了再打开
@pytest.fixture
def reopen_file(check_app_respone):
    def inner(file_name):
        pyautogui.click(185, 15)
        time.sleep(1)
        pyperclip.copy(file_name)
        pyautogui.hotkey("ctrl", "v")
        pyautogui.press("enter")
        return check_app_respone("FILES", "LOAD_PROJECT", 0, 10)
    return inner

# 连续打开
@pytest.fixture
def continuous_open_file(check_app_respone):
    def inner(file_name):
        pyautogui.click(185, 15)
        time.sleep(1)
        pyperclip.copy(file_name)
        pyautogui.hotkey("ctrl", "v")
        pyautogui.press("enter")
        return check_app_respone("FILES", "LOAD_PROJECT", 0, 10)
    return inner



def test_reopen_3mf(reopen_item, baseline_cur_path, reopen_file):
    path = os.path.join(baseline_cur_path, reopen_item["input"])
    assert reopen_file(path)["code"] == reopen_item["expectation"]


def test_continuous_3mf(continuous_item, baseline_cur_path, continuous_open_file):
    path = os.path.join(baseline_cur_path, continuous_item["input"])
    ret = continuous_open_file(path)
    assert ret["code"] == continuous_item["expectation"]
