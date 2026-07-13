.PHONY: all pdf

all: pdf

pdf: build/main.pdf

build/main.pdf: main.tex intro.tex interface_manual.tex
	mkdir -p build
	latexmk -pdf -interaction=nonstopmode -halt-on-error -outdir=build main.tex
