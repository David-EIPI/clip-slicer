# SPDX-FileCopyrightText: 2026 David Shirvanyants
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

.PHONY: all pdf

all: pdf

pdf: build/main.pdf

build/main.pdf: main.tex intro.tex interface_manual.tex
	mkdir -p build
	latexmk -pdf -interaction=nonstopmode -halt-on-error -outdir=build main.tex
