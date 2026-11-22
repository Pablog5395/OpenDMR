#!/usr/bin/env python3
"""Regenerates the UI Roboto fonts with Latin-1 and Latin Extended-A (the accented letters of Western and Central
European languages, Turkish ...).

  python tools/fonts/regen_fonts.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TTF = os.path.join(HERE, 'ttf', 'RobotoCondensed-VF.ttf')
MKFONT = os.path.join(HERE, 'mkfont.py')

# (variable_name, size, weight, extra_args)
FONTS = [
    ('font_roboto12', 12, 'Regular', ['--latin-ext']),
    ('font_roboto12_bold', 12, 'Bold', ['--latin-ext']),
    ('font_roboto14', 14, 'Regular', ['--latin-ext']),
    ('font_roboto17', 17, 'Regular', ['--latin-ext']),
    ('font_roboto17_bold', 17, 'Bold', ['--latin-ext']),
    ('font_roboto20', 20, 'Regular', ['--latin-ext']),
    ('font_roboto22', 22, 'Regular', ['--latin-ext']),
    ('font_roboto32_bold', 32, 'Bold', ['--latin-ext']),
    ('font_roboto44_bold', 44, 'Bold', []),  # ASCII only: times, frequencies, callsigns (qso_panel.c: 32 px otherwise)
    ('font_roboto42_bold', 42, 'Bold', ['--chars', '0123456789. ']),
]

for name, size, weight, extra in FONTS:
    cmd = [sys.executable, MKFONT, TTF, str(size), name, '--weight', weight] + extra
    subprocess.check_call(cmd)
