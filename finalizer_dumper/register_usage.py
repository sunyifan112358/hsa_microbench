#!/usr/bin/env python

import sys;
import re;

def isa_register_usage(isa_file_name):
	try: 
		isa = open(isa_file_name, "r")
	except:
		print "Error: open file " + isa_file_name

# Init values
	v_reg = 0
	s_reg = 0

	lines = isa.readlines()

	v_reg = max_reg('v', lines)
	s_reg = max_reg('s', lines)

	print "vector register: " + str(v_reg)
	print "scalar register: " + str(s_reg)

	return v_reg, s_reg
	


def max_reg(reg_prefix, lines):
	max_num = 0;
	re_reg = re.compile(r"(?:" + reg_prefix + r"([0-9]+))|(?:" + 
		reg_prefix + r"\[([0-9]+):([0-9]+)\])")

	for line in lines:
		matches = re.findall(re_reg, line)
		for match in matches:
			for num_string in match:
				if num_string != '':
					num = int(num_string)
					if num > max_num:
						max_num = num

	return max_num

				
def main():
	isa_register_usage(sys.argv[1])	


if __name__ == "__main__":
	main()
