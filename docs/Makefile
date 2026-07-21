# SPDX-FileCopyrightText: 2026 David Shirvanyants
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

SOURCES := main.tex intro.tex interface_manual.tex detect_algorithm.tex \
	optimize_algorithm.tex generate_algorithm.tex

.PHONY: all pdf html embedded

all: pdf

pdf: build/main.pdf

html: build/main.html

embedded: ../app/embedded_manual.inc

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

../app/embedded_manual.inc: build/main.html embed_html.cmake
	cmake -DINPUT=$(CURDIR)/$< -DOUTPUT=$(abspath $@) -P embed_html.cmake
