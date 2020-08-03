#!/usr/bin/env python

from setuptools import setup, Extension

ext = Extension(
    "poqaio._poqaio",
    sources=["extension/module.c", "extension/protocol.c"],
    depends=["protocol.h"],
)

setup(ext_modules=[ext])
