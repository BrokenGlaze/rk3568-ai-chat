#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 LVGL 拼音"词库" pinyin_word_dict.c（配合 lv_ime_pinyin 词级匹配）

数据源(自动下载):
  - CC-CEDICT (cc-cedict.org)  一词一拼音, 含多音字

过滤规则:
  - 只保留 2~5 个常用字组成的词(所有字都出现在字频表前 3500 名 → 常用字)
  - 每个拼音键最多保留 6 个词(候选面板一页), 按"词内字频之和"排序(常用词靠前)
  - ü -> v (LVGL 惯例), 去声调数字, 拼音拼接成串

产物: src/pinyin_word_dict.c
      const lv_pinyin_dict_t ds_pinyin_word_dict[] = { {py, "词 词 词"}, ... {NULL,NULL} };
      const uint32_t ds_pinyin_word_num = N;
用法:  python3 mytools/gen_word_dict.py
"""
import os
import re
import gzip
import urllib.request

BASE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(BASE, "pinyin_data")
CEDICT_URL = "https://www.mdbg.net/chinese/export/cedict/cedict_1_0_ts_utf-8_mdbg.txt.gz"
CEDICT_GZ  = os.path.join(DATA, "cedict.txt.gz")
OUT        = os.path.normpath(os.path.join(BASE, "..", "src", "pinyin_word_dict.c"))
MAX_WORDS_PER_KEY = 6      # 候选面板一页数量
COMMON_CHAR_RANK   = 3500  # 字频前 3500 视为"常用字"

def fetch(url, path):
    if not os.path.exists(path):
        os.makedirs(DATA, exist_ok=True)
        print("下载:", url)
        urllib.request.urlretrieve(url, path)

def strip_tone(syl):
    # 去声调数字, ü->v, 小写
    syl = syl.lower()
    while syl and syl[-1].isdigit():
        syl = syl[:-1]
    syl = syl.replace("ü", "v").replace("u:", "v")
    return syl

def is_basic_cjk(ch):
    return '\u4e00' <= ch <= '\u9fff'

fetch(CEDICT_URL, CEDICT_GZ)

# ---- 1. 字频表(排序用, 复用 pinyin_data/frequence.txt, 没有就现下载) ----
FREQ_TXT = os.path.join(DATA, "frequence.txt")
if not os.path.exists(FREQ_TXT):
    fetch("https://cdn.jsdelivr.net/gh/techkang/chinese_character_frequence@master/frequence.txt", FREQ_TXT)
char_rank = {}
for i, line in enumerate(open(FREQ_TXT, encoding="utf-8")):
    ch = line.split("\t", 1)[0].strip()
    if len(ch) == 1 and is_basic_cjk(ch):
        char_rank.setdefault(ch, i)

# ---- 2. 解析 CC-CEDICT ----
# 格式: 繁體 简体 [pin1 yin1] /释义/
key_words = {}     # 拼音键 -> set(词)
with gzip.open(CEDICT_GZ, "rt", encoding="utf-8") as f:
    for line in f:
        if line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 3:
            continue
        simp = parts[1]
        n = len(simp)
        if n < 2 or n > 5:
            continue
        if not all(is_basic_cjk(c) for c in simp):
            continue
        # 只收"常用字"组成的词(直接满足"得到的字都是常用字")
        if not all(c in char_rank and char_rank[c] < COMMON_CHAR_RANK for c in simp):
            continue
        # 拼音在"繁体 简体 [pin yin]"位置, 只取第一对中括号(避免吞进释义里的[...])
        m = re.search(r"\[([^\]\[]*)\]", line)
        if not m:
            continue
        syls = [strip_tone(s) for s in m.group(1).split()]
        if any(not s for s in syls):
            continue
        key = "".join(syls)
        key_words.setdefault(key, set()).add(simp)

# ---- 3. 排序: 词内字频之和(越小越常用) ----
def word_score(w):
    return sum(char_rank.get(c, 10**6) for c in w)

items = []
for key, words in key_words.items():
    ranked = sorted(words, key=lambda w: (word_score(w), len(w), w))
    items.append((key, ranked[:MAX_WORDS_PER_KEY]))
items.sort(key=lambda kv: kv[0])

# ---- 4. 输出 C 源文件 ----
with open(OUT, "w", encoding="utf-8") as f:
    f.write("/* 由 mytools/gen_word_dict.py 生成 —— LVGL 拼音词库(词级输入) */\n")
    f.write("/* 数据源: CC-CEDICT(cc-cedict.org) + 汉字字频表 */\n\n")
    f.write('#include "lvgl/lvgl.h"\n\n')
    f.write("const uint32_t ds_pinyin_word_num = %d;\n\n" % len(items))
    f.write("const lv_pinyin_dict_t ds_pinyin_word_dict[] = {\n")
    for key, words in items:
        f.write('    { "%s", "%s" },\n' % (key, " ".join(words)))
    f.write("    { NULL, NULL }\n")
    f.write("};\n")

print("词库键数: %d  总词条: %d" % (len(items), sum(len(w) for _, w in items)))
print("输出: %s" % OUT)
