#!/usr/bin/env python3
"""
并发提交性能测试：向 /api/submissions 发起 N 次并发提交，轮询结果，统计完成时延分位数。
用法: python3 tests/perf/perf_test.py
"""
import json
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib import request

BASE_URL = 'http://127.0.0.1:8080'
NUM = 50
PROBLEM_ID = 900003
LANGUAGE_ID = 1
USER_BASE = 2001

SOURCE_CODE = r'''#include <bits/stdc++.h>
using namespace std;
int main(){int a,b; if(!(cin>>a>>b)) return 0; cout<<a+b;}
'''

def post_submission(user_id):
    url = BASE_URL + '/api/submissions'
    body = {
        'source_code': SOURCE_CODE,
        'problem_id': PROBLEM_ID,
        'language_id': LANGUAGE_ID,
        'user_id': user_id,
        'mode': 'submit'
    }
    data = json.dumps(body).encode('utf-8')
    req = request.Request(url, data=data, headers={'Content-Type': 'application/json'})
    start = time.time()
    with request.urlopen(req, timeout=10) as resp:
        text = resp.read().decode('utf-8')
    j = json.loads(text)
    sid = j.get('id')
    return sid, start

def poll_submission(sid, start):
    url = BASE_URL + f'/api/submissions/{sid}'
    req = request.Request(url)
    while True:
        with request.urlopen(req, timeout=10) as resp:
            text = resp.read().decode('utf-8')
        j = json.loads(text)
        status = j.get('status')
        if status not in ('pending', 'running'):
            end = time.time()
            return status, end - start
        time.sleep(0.1)

def run():
    print('Starting', NUM, 'concurrent submissions')
    results = []
    with ThreadPoolExecutor(max_workers=50) as ex:
        # use an existing user id to avoid FK constraint failures
        futures = [ex.submit(post_submission, 2001) for i in range(NUM)]
        submitted = []
        for fut in as_completed(futures):
            try:
                sid, start = fut.result()
                if sid is None:
                    print('submission failed, no id')
                    continue
                submitted.append((sid, start))
            except Exception as e:
                print('post error', e)

    # Polling
    with ThreadPoolExecutor(max_workers=100) as ex:
        futures = [ex.submit(poll_submission, sid, start) for sid, start in submitted]
        for fut in as_completed(futures):
            try:
                status, elapsed = fut.result()
                results.append((status, elapsed))
            except Exception as e:
                print('poll error', e)

    times = [t for s,t in results]
    times.sort()
    n = len(times)
    def pct(p):
        if n==0: return None
        idx = max(0, min(n-1, int(p/100.0 * n)))
        return times[idx]

    print('total submissions:', n)
    if n:
        print('min:', times[0])
        print('p50:', pct(50))
        print('p90:', pct(90))
        print('p95:', pct(95))
        print('p99:', pct(99))
        print('max:', times[-1])
    # count accepted
    from collections import Counter
    cnt = Counter(s for s,t in results)
    print('status counts:', dict(cnt))

if __name__ == '__main__':
    run()
