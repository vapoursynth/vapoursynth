#!/usr/bin/env python

import os, stat, urllib2

f = urllib2.urlopen('http://waf.googlecode.com/files/waf-1.7.5')

with open('waf', 'wb') as waf:
    waf.write(f.read())

os.chmod('waf', os.stat('waf').st_mode | stat.S_IXUSR)
