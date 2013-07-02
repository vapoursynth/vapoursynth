#!/usr/bin/env python

import os, stat, urllib.request

urllib.request.urlretrieve('https://waf.googlecode.com/files/waf-1.7.11', 'waf')
os.chmod('waf', os.stat('waf').st_mode | stat.S_IXUSR)
