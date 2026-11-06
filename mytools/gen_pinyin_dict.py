#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 LVGL lv_ime_pinyin 自定义字典 src/pinyin_dict.h

数据源(自动下载到本目录 pinyin_data/):
  - mozillazg/pinyin-data 的 pinyin.txt      (全汉字全部读音, 带声调)
  - techkang/chinese_character_frequence 的 frequence.txt (汉字字频, 决定候选排序)

用法:  python3 mytools/gen_pinyin_dict.py
产物:  src/pinyin_dict.h   (static lv_pinyin_dict_t ds_pinyin_dict[], 425 音节)
在 screen.c 里用 lv_ime_pinyin_set_dict(pinyin_ime, ds_pinyin_dict) 接入。
"""
import os
import re
import urllib.request

BASE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(BASE, "pinyin_data")
PINYIN_URL = "https://raw.githubusercontent.com/mozillazg/pinyin-data/master/pinyin.txt"
FREQ_URL   = "https://cdn.jsdelivr.net/gh/techkang/chinese_character_frequence@master/frequence.txt"
PINYIN_TXT = os.path.join(DATA, "pinyin.txt")
FREQ_TXT   = os.path.join(DATA, "frequence.txt")
OUT        = os.path.normpath(os.path.join(BASE, "..", "src", "pinyin_dict.h"))
MAX_CAND   = 15          # 每个音节候选上限(候选面板每页 6 个)

def fetch(url, path):
    if not os.path.exists(path):
        os.makedirs(DATA, exist_ok=True)
        print("下载:", url)
        urllib.request.urlretrieve(url, path)

fetch(PINYIN_URL, PINYIN_TXT)
fetch(FREQ_URL, FREQ_TXT)

# 去声调: 带调字母 -> 无调; ü(含带调) -> v (LVGL 惯例 lv/nv; j/q/x/y 后已是 u)
TONE = str.maketrans(
    "āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜńňǹḿ",
    "aaaaeeeeiiiioooouuuuüüüünnnm")

def strip_tone(py):
    return py.translate(TONE).replace("ü", "v")

# ---- 1. pinyin.txt: 码点 -> 全部无调读音 ----
char_readings = {}
for line in open(PINYIN_TXT, encoding="utf-8"):
    line = line.split("#")[0].strip()
    m = re.match(r"U\+([0-9A-Fa-f]+):\s*(.*)", line)
    if not m:
        continue
    cp = int(m.group(1), 16)
    if not (0x4E00 <= cp <= 0x9FFF):      # 只要基本汉字(UTF-8 3字节)
        continue
    ch = chr(cp)
    s = set()
    for p in m.group(2).split(","):
        p = strip_tone(p.strip())
        # 只保留 a-z 开头(排除 ê 等非 ASCII)，v 开头音节也无法输入
        if p and 'a' <= p[0] <= 'z' and not p.startswith("v"):
            s.add(p)
    if s:
        char_readings[ch] = s

# ---- 2. 字频表: 行号即字频排名(越靠前越常用) ----
rank = {}
for i, line in enumerate(open(FREQ_TXT, encoding="utf-8")):
    ch = line.split("\t", 1)[0].strip()
    if len(ch) == 1 and '\u4e00' <= ch <= '\u9fff':
        rank.setdefault(ch, i)

# ---- 3. 反转为 音节 -> [(rank, char)] ----
syl = {}
for ch, pys in char_readings.items():
    r = rank.get(ch, len(rank) + ord(ch))  # 字频表之外的生僻字排后面
    for p in pys:
        syl.setdefault(p, []).append((r, ch))

# ---- 4. 每音节: 去重 -> 按常用度排序 -> 截断 ----
for p in syl:
    seen = {}
    for r, c in syl[p]:
        seen.setdefault(c, r)              # 保留该字最好(最小)的 rank
    items = sorted(seen.items(), key=lambda kv: (kv[1], ord(kv[0])))
    syl[p] = [c for c, _ in items[:MAX_CAND]]

# ---- 5. 输出 C 头文件 ----
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    f.write("/* 由 mytools/gen_pinyin_dict.py 生成 —— LVGL lv_ime_pinyin 自定义字典 */\n")
    f.write("/* 数据源: mozillazg/pinyin-data (pinyin.txt) + 汉字字频表 */\n")
    f.write("/* 需在包含 lvgl 头文件之后使用(依赖 lv_pinyin_dict_t) */\n\n")
    f.write("static lv_pinyin_dict_t ds_pinyin_dict[] = {\n")
    for p in sorted(syl):
        f.write('    { "%s", "%s" },\n' % (p, "".join(syl[p])))
    f.write("    { NULL, NULL }\n")
    f.write("};\n")

print("音节数: %d  候选字总数: %d" % (len(syl), sum(len(v) for v in syl.values())))
print("输出: %s" % OUT)
