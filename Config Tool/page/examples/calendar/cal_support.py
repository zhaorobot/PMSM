#! /usr/bin/env python
#
# Support module generated by PAGE version 4.2g
# In conjunction with Tcl version 8.6
#    Feb. 11, 2014 11:49:23 AM


import sys

try:
    from Tkinter import *
except ImportError:
    from tkinter import *

try:
    import ttk
    py3 = 0
except ImportError:
    import tkinter.ttk as ttk
    py3 = 1

import datetime
import calendar

def init(top, gui, arg=None):
    global w, top_level, root
    w = gui
    top_level = top
    root = top
    current()

def destroy_window ():
    # Function which closes the window.
    global top_level
    top_level.destroy()
    top_level = None

def current():
    w.today = datetime.date.today()
    load_cal(w.today)
    pass

def last():
    month = w.date.month - 1
    year = w.date.year
    day = w.date.day
    if month == 0:
        month = 12
        year -= 1
    date = datetime.date(year, month, day)
    load_cal(date = date)

def next():
    month = w.date.month + 1
    year = w.date.year
    day = w.date.day
    if month == 13:
        month = 1
        year += 1
    date = datetime.date(year, month, day)
    load_cal(date = date)

def quit():
    sys.exit()

def load_cal(date):

    obj = w.Text1
    w.date = date
    calendar.setfirstweekday(calendar.SUNDAY)
    cal_str = calendar.month(date.year, date.month)
    obj.delete(1.0,END)
    obj.insert(END,cal_str)
    if date.year == w.today.year and date.month == w.today.month:
        # Color today's day if month and year are current month and year.
        day = str(date.day)
        obj.tag_configure('e0', foreground='red')
        obj.tag_remove('e0', 1.0, END)
        start = cal_str.find(day,15)  # Skip month and year
        end = start + len(day)
        min_c = '%d.0+%dchars' % (1, start)
        max_c = '%d.0+%dchars' % (1, end)
        obj.tag_add('e0', min_c, max_c)



