#!/usr/bin/env python

import sys;

def isa_num_instruction(isa_file_name):
	try:
		isa = open(isa_file_name, "r")
	except:
		print "Error: open file " + isa_file_name

# Init values
	num_lines = 0;
	
	lines = isa.readlines()
	for line in lines:
		if line.strip() and "//" in line:
			num_lines += 1
			
	print "Num of instructions: " + str(num_lines)

	return num_lines


def main():
	isa_num_instruction(sys.argv[1])


if __name__ == "__main__":
	main()
