# SPDX-FileCopyrightText: 2026 David Shirvanyants
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

SOURCES := main.tex intro.tex interface_manual.tex detect_algorithm.tex \
	optimize_algorithm.tex generate_algorithm.tex

.PHONY: all pdf html

all: pdf

pdf: build/main.pdf

html: build/main.html

build/main.pdf: $(SOURCES)
	mkdir -p build
	latexmk -pdf -interaction=nonstopmode -halt-on-error -outdir=build main.tex

build/main.html: $(SOURCES) help.css
	mkdir -p build
	pandoc main.tex \
		--from=latex \
		--to=html5 \
		--standalone \
		--toc \
		--mathml \
		--css=help.css \
		--embed-resources \
		--metadata=lang:en-US \
		--output=$@
