cmake_minimum_required(VERSION 3.21)
project(pci_driver NONE)
kmod_target(edu_pci SOURCES pci_edu.c)
