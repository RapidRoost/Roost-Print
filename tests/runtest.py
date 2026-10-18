
#!/usr/bin/python3

## runtest.py
# The runtest.py script runs regression tests on the CuraEngine.

import argparse
import ast  # For safe function evaluation.
import json
import os
import random
import subprocess
import sys
import threading
import time
import pytest
from xml.etree import ElementTree
## The TestResults class stores a group of TestSuite objects, each TestSuite object contains failed and successful test.
#  This class can output the result of the tests in a JUnit xml format for parsing in Jenkins.
class TestResults:
    def __init__(self):
        self._testsuites = []

    ## Create a new test suite with the name.
    def addTestSuite(self, name):
        suite = TestSuite(name)
        self._testsuites.append(suite)
        return suite

    def getFailureCount(self):
        result = 0
        for testsuite in self._testsuites:
            result += testsuite.getFailureCount()
        return result

    ## Save the test results to the file given in the filename.
    def saveXML(self, filename):
        xml = ElementTree.Element("testsuites")
        xml.text = "\n"
        for testsuite in self._testsuites:
            testsuite_xml = ElementTree.SubElement(xml, "testsuite",
                                                   {"name": testsuite._name, "errors": "0", "tests": str(testsuite.getTestCount()),
                                                    "failures": str(testsuite.getFailureCount()), "time": "0",
                                                    "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime())})
            testsuite_xml.text = "\n"
            testsuite_xml.tail = "\n"
            for class_name, test_name in testsuite._successes:
                testcase_xml = ElementTree.SubElement(testsuite_xml, "testcase", {"classname": class_name, "name": test_name})
                testcase_xml.text = "\n"
                testcase_xml.tail = "\n"
            for class_name, test_name, error_message in testsuite._failures:
                testcase_xml = ElementTree.SubElement(testsuite_xml, "testcase", {"classname": class_name, "name": test_name})
                testcase_xml.text = "\n"
                testcase_xml.tail = "\n"
                failure = ElementTree.SubElement(testcase_xml, "failure", {"message": "test failure"})
                failure.text = error_message
                failure.tail = "\n"
        return ElementTree.ElementTree(xml).write(filename, "utf-8", True)


class EngineTest:
    def __init__(self, json_filename, engine_filename, models):
        self._json_filename = json_filename
        self._json = json.load(open(json_filename, "r"))
        self._locals = {}
        self._addAllLocals()  # Fills the _locals dictionary.
        self._addLocalsFunctions()  # Add mock functions used in fdmprinter
        self._engine = engine_filename
        self._models = models
        self._settings = {}
        self._test_results = TestResults()

    def _runProcess(self, cmd):
        p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        p.error = ""
        t = threading.Thread(target=self._abortProcess, args=(p,))
        p.aborting = False
        t.start()
        stdout, stderr = p.communicate()
        p.aborting = True
        if p.error == "Timeout":
            return "Timeout: %s" % (' '.join(cmd))
        if p.wait() != 0:
            return "Execution failed: %s" % (' '.join(cmd))
        return None

    def _abortProcess(self, p):
        for i in range(0, 60):
            time.sleep(1)  # Check every 1000ms if we need to abort the thread.
            if p.aborting:
                break
        if p.poll() is None:
            p.terminate()
            p.error = "Timeout"

    def getResults(self):
        return self._test_results

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="testing script")
    parser.add_argument("json", type=str, help="JSON file to use")
    parser.add_argument("engine", type=str, help="Engine executable")
    pytest.main(["-s","-v"])
    print("All tests passed")
