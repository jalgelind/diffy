#!/usr/bin/env python3
import os, re, sys
import argparse
import itertools
import shutil
import subprocess

RUNROOT = "out"
WORKDIR = "out/workdir"
ARCHIVE_FAILED = "out/failed"

class Process:
    def __init__(self, cmdline, workdir):
        from subprocess import Popen, PIPE, STDOUT
        from threading import Lock
        self._done = False
        self._process_handle = Popen(cmdline,
            shell=True, executable='/bin/bash', cwd=workdir, stdout=PIPE, stderr=STDOUT)

    def wait(self):
        if not self._done:
            stdout, _ = self._process_handle.communicate()
            self._stdout = stdout.decode('utf-8', 'replace')
            self._return_code = self._process_handle.returncode
            self._done = True
        return self

    @property
    def return_code(self):
        return self.wait()._return_code

    @property
    def stdout(self):
        return self.wait()._stdout

def mkdirs(path):
    try:
        os.makedirs(path)
    except:
        pass

def rmdirs(path):
    try:
        shutil.rmtree(path)
    except:
        pass

def sha1sum(path):
    import hashlib
    sha1 = hashlib.sha1()
    sha1.update(open(path, "rb").read())
    return sha1.hexdigest()

def grouper(iterable, n, fillvalue=None):
    "Collect data into fixed-length chunks or blocks"
    # grouper('ABCDEFG', 3, 'x') --> ABC DEF Gxx
    args = [iter(iterable)] * n
    return itertools.zip_longest(fillvalue=fillvalue, *args)

def collect_test_cases(tests_directory):
    candidates = []
    for root, dirs, files in os.walk(tests_directory):
        for file in files:
            if re.search("([ab])$", file):
                candidates.append(os.path.join(root, file))

    # TODO: This isn't the most robust way to group the test cases. Fix?
    keyfunc = lambda path: os.path.dirname(path)
    def group_list(ungrouped_list):
        for a, b in grouper(ungrouped_list, 2, None):
            if a and a.endswith('a') and b and b.endswith('b'):
                yield (a, b)
            else:
                sys.exit("ERROR: found non-matching pair ({}, {})".format(a, b))
    return {k:list(group_list(sorted(g))) \
                for k, g in itertools.groupby(sorted(candidates,
                                                     key = keyfunc)
                                              ,keyfunc)}

def run_single_crash_test(args, test_group, name, test_case):
    a_orig, b_orig = test_case
    p = Process(f"{args.diff_tool} {args.args} {a_orig} {b_orig}", ".").wait()
    if p.return_code != 0:
        print(f"  Test '{test_group}/{name}' FAILED to execute properly")
        print(f"    {args.diff_tool} {args.args} {a_orig} {b_orig}")
        print(p.stdout)

def run_single_patch_test(args, test_group, name, test_case):
    # TODO(parallelism): Must be a unique workdir.
    rmdirs(WORKDIR)
    mkdirs(WORKDIR)
    a_orig, b_orig = test_case
    a_test, b_test = [os.path.join(WORKDIR, os.path.basename(f)) for f in test_case]
    a_base, b_base = list(map(os.path.basename, test_case))
    
    shutil.copy(a_orig, a_test)
    shutil.copy(b_orig, b_test)

    patch_file = f"{name}.patch"
    Process(f"{args.diff_tool} {args.args} {a_base} {b_base} > {patch_file}", WORKDIR).wait()
    patch_output = Process(f"patch -u -p0 --input={patch_file}", WORKDIR).stdout

    patch_ok = sha1sum(a_test) == sha1sum(b_test)

    if not patch_ok:
        print(f" Test '{name}' FAILED")
        
        target = os.path.join(ARCHIVE_FAILED, args.config_name + "_" + test_group.replace("/", "_"))
        dest = os.path.join(target, name)
        
        mkdirs(dest)
        
        def copy(src, dst):
            cp_cmd = f'cp -r "{src}" "{dst}"'
            cp_result = Process(cp_cmd, workdir=".").return_code
            if cp_result != 0:
                print(cp_cmd)
                print(f"  Failed to copy WORKDIR ('{WORKDIR}')")
                print(f"                 to DEST ('{dest}')")
                print(cp_result)
        copy(WORKDIR, dest)


def main(argv):
    parser = argparse.ArgumentParser(description="Run testsuite on given binary. Diff two files and attempt to run `patch` with the given diff.")
    parser.add_argument('diff_tool', action='store', help='Diff tool to test')
    parser.add_argument('--show-test-list', dest='show_test_list', action='store_true', help='Show list of tests')
    args = parser.parse_args()

    all_test_cases = collect_test_cases("test_cases")

    if args.show_test_list:
        import pprint
        pprint.pprint(all_test_cases)
        return

    rmdirs(RUNROOT)

    # TODO: Basic benchmarking

    # TODO: Help scripts in the failed test archive, or at least
    #       more output, or comparison with a "good" diff.

    # TODO: Something to re-run a single test case

    patch_configs = (
        *['-a mg -U{}'.format(n) for n in range(2)],
        *['-a ml -U{}'.format(n) for n in range(2)],
        *['-a p -U{}'.format(n) for n in range(2)]
    )

    crash_configs = (
        #*['-a p -S0 -W {}'.format(n) for n in range(0, 100, 3)],
    )

    for config in crash_configs:
        run_args = argparse.Namespace(
            diff_tool = os.path.abspath(args.diff_tool),
            args = config,
            config_name = config
        )

        print(f"Test crashiness for configuration '{run_args.args}'")
        for test_group, test_cases in all_test_cases.items():
            for test_case in test_cases:
                name = os.path.basename(test_case[0])[:-1]
                run_single_crash_test(run_args, test_group, name, test_case)

    
    for config in patch_configs:
        run_args = argparse.Namespace(
            diff_tool = os.path.abspath(args.diff_tool),
            args = config,
            config_name = config.replace('-a ', '').replace(' -', '_').replace('test_cases/', '')
        )

        print(f"Running tests for configuration '{run_args.args}'")
        for test_group, test_cases in all_test_cases.items():
            for test_case in test_cases:
                name = os.path.basename(test_case[0])[:-2]
                run_single_patch_test(run_args, test_group, name, test_case)

if __name__ == '__main__':
    main(sys.argv)
