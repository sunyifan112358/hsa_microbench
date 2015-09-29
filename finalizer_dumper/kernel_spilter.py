#!/usr/bin/env python
from __future__ import print_function

import sys
import re
import os
import subprocess
from register_usage import *
from num_instruction import *

def split_kernel(filename):
	try:  
		cl_file = open(filename, "r")	
	except:
		print("Error: open file " + filename)

	v_reg_sum = 0
	s_reg_sum = 0
	num_inst_sum = 0

# Line by line look for kernel
	lines = cl_file.readlines()
	re_kernel = re.compile(r"__kernel(?: +)void(?: +)([\w_]+)")
	kernel_file = None
	global_lines = []
	for line in lines:
		matches = re.search(re_kernel, line)
		if matches:

			# Finish previous file
			if kernel_file:
				kernel_file.close()
				v_reg, s_reg, num_inst = kernel_register_usage(kernel_file)
				v_reg_sum += v_reg;
				s_reg_sum += s_reg;
				num_inst_sum += num_inst

	
			# Get kernel name
			kernel_name = matches.group(1)

			# Create a single kernel cl file
			kernel_file = open(kernel_name + ".cl", "w")
			
			# Write global lines to each file
			for global_line in global_lines:
				kernel_file.write(global_line);
			
			# Write kernel declaration line
			kernel_file.write(line)

		# Write kernel contents
		elif kernel_file:
			kernel_file.write(line)

		# Stores global declarations
		else:
			global_lines.append(line);

	kernel_file.close()
	v_reg, s_reg, num_inst = kernel_register_usage(kernel_file)
	v_reg_sum += v_reg;
	s_reg_sum += s_reg;
	num_inst_sum += num_inst

	print("vector register total: " + str(v_reg_sum))
	print("scalar register total: " + str(s_reg_sum))
	print("number instructions total: " + str(num_inst_sum))
	

def kernel_register_usage(kernel_file):
	kernel_name = kernel_file.name[0:-3]
	print(kernel_name)
	command = "cloc.sh -hsail " + kernel_name + ".cl"
	print(command)
	os.system(command);
	os.system("finalizer_dumper " + kernel_name + ".brig")
	os.system("mv isa.txt " + kernel_name + ".isa")
	v_reg, s_reg = isa_register_usage(kernel_name + ".isa")
	num_inst = isa_num_instruction(kernel_name + ".isa")
#os.system("rm isa.txt")
	return v_reg, s_reg, num_inst
	

def main():
	split_kernel(sys.argv[1])


if __name__ == "__main__":
	main()
