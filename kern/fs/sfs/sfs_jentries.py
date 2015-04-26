#!/usr/bin/python

import re
import shutil

struct_start = re.compile("struct ([a-z_]+)_args {")
struct_end = re.compile("};")

infile = open("sfs_jentries.c", "rb")
outfile = open("temp", "wb")


def main():
	global infile
	struct = None
	structs = {}
	output = True
	for line in infile:
		#line = line[:-1]
		if output:
			write(line)
		if struct_start.match(line):
			struct = struct_start.search(line).group(1)
			structs[struct] = []
		elif struct and struct_end.match(line):
			struct = None
		elif struct and "\t" in line:
			line = re.sub("[;\t\n]", "", line)
			tokens = line.split(" ")
			if "*" in line:
				tokens = line.split("*")
				tokens[0] += "*"
			structs[struct].append(tokens)
			#print {"ctype": tokens[0], "name": tokens[1]}
		elif "/* Autogenerate cases: sfs_jentries.py */" in line:
			gen_cases(structs)
			output = False
		elif "/* Autogenerate functions: sfs_jentries.py */" in line:
			gen_functions(structs)
			output = False
		elif "/* End autogenerate */" in line:
			output = True
			write(line)

def write(line):
	global outfile
	outfile.write(line)

def gen_cases(structs):
	for struct in structs:
		print_format = []
		print_args = []
		for ctype, name in structs[struct]:
			if "void" in ctype:
				print_format.append("%s=%%p" % name)
			else:
				print_format.append("%s=%%d" % name)
			print_args.append("((struct %s_args*)rec)->%s" % (struct, name))
		fmt = {
			"struct_name": struct,
			"struct_name_upper": struct.upper(),
			"print_format": ", ".join(print_format),
			"print_args": ",\n\t\t\t\t".join(print_args)
		}
		write("""		case %(struct_name_upper)s:
			len = sizeof(struct %(struct_name)s_args);
			kprintf("%(struct_name_upper)s(%(print_format)s)",
				%(print_args)s);
			break;\n""" % fmt)

def gen_functions(structs):
	for struct in structs:
		initializations = []
		entry_args = []
		for ctype, name in structs[struct]:
			if "code" in name:
				continue
			entry_args.append("%s %s" % (ctype, name))
			initializations.append("record->%s = %s;" % (name, name))

		fmt = {
			"struct_name": struct,
			"struct_name_upper": struct.upper(),
			"entry_args": ", ".join(entry_args),
			"initializations": "\n\t".join(initializations)
		}

		write("""void *jentry_%(struct_name)s(%(entry_args)s)
{
	struct %(struct_name)s_args *record;

	record = kmalloc(sizeof(struct %(struct_name)s_args));
	record->code = %(struct_name_upper)s;
	%(initializations)s

	return (void *)record;
}\n\n""" % fmt)

if __name__ == "__main__":
	print "Generating..."
	main()
	infile.close()
	outfile.close()
	shutil.copyfile("temp", "sfs_jentries.c")
	print "Done."