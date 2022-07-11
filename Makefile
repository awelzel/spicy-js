.PHONY: check
SHELL := /bin/bash
DOCKER ?= docker

check:
	$(DOCKER) build -f ./docker/debian11.Dockerfile .
