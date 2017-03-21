#!/usr/bin/env python3

#I don't really like bringing in Python, but the alternative is trying to express this in C++ preprocessor,
#which would be ugly both in use (LDA(I_arch) JNE(ARCH_TRUE, die) LABEL(die))
#and implementation (there's no obvious way to represent those labels)
#so this is the best I can do

#this mostly follows the assembly language specified in
# https://www.kernel.org/doc/Documentation/networking/filter.txt
#with a few exceptions:
#  je followed by jmp is merged (making combined je/jne unneeded)
#    as such, addressing mode 7 (combined je/jne) is not implemented; it's harder to read
#      instead, jnset was added
#  ja and jneq are removed, use jmp and jne
#  unconditional jump to return is optimized
#  je out-of-bounds is converted to jne+jmp
#  addressing modes 4 and 8 (immediate constants) pass their argument out unchanged to the C compiler, so you can use __NR_exit
#comments are supported via ; (anywhere) or # (start of line only), /* */ not supported
#seccomp-specific docs: https://www.kernel.org/doc/Documentation/prctl/seccomp_filter.txt

#upstream assembler source code:
# http://lxr.free-electrons.com/source/tools/net/bpf_exp.y

import os, sys

if len(sys.argv)==1:
	bpf = sys.stdin.read()
	outfile = sys.stdout
if len(sys.argv)==2:
	bpf = open(sys.argv[1], "rt").read()
	outfile = open(os.path.splitext(sys.argv[1])[0]+".inc", "wt")
	sys.stdout
if len(sys.argv)==3:
	bpf = open(sys.argv[1], "rt").read()
	outfile = open(sys.argv[2], "wt")

#"pass" 1: compile list of opcodes and addressing modes
#output: ops = { "ld": { "M[_]": "BPF_LD|BPF_W|BPF_MEM" } }
import re
addrmodes = [
	('-', "", ""),
	('0', "x", "|BPF_X"),
	('0', "%x", "|BPF_X"),
	('1', "[_]", "|BPF_K"),
	('2', "[x+_]", "|BPF_IND"),
	('3', "M[_]", "|BPF_MEM"),
	('4', "#_", "|BPF_IMM"),
	('5', "4*([_]&0xf)", "|BPF_LEN"),
	# 6 is label, it's hardcoded
	# 7 is merged compare/true/false, not implemented
	# 8 is compare/fallthrough, also hardcoded
	('9', "a", "|BPF_A"),
	('9', "%a", "|BPF_A"),
	# 10 is various pseudoconstants like packet length, not available for BPF
	]
ops = {}
for op,val,modes in [
	("ld",  "BPF_LD|BPF_W", "1234"),
	("ldi", "BPF_LD|BPF_W", "4"), # the linux bpf assembler allows this without #, as an undocumented extension
	("ldh", "BPF_LD|BPF_H", "12"),
	("ldb", "BPF_LD|BPF_B", "12"),
	("ldx",  "BPF_LDX|BPF_W", "345"),
	("ldxi", "BPF_LDX|BPF_W", "4"),
	("ldxb", "BPF_LDX|BPF_B", "5"),
	("st",  "BPF_ST", "3"),
	("stx", "BPF_STX", "3"),
	("jmp", "BPF_JMP|BPF_JA",  None), # for branches, the third param is whether the label goes to True or False (the other falls through)
	("jeq", "BPF_JMP|BPF_JEQ", True), # (JMP behaves specially, so None)
	("jne", "BPF_JMP|BPF_JEQ", False),
	("jlt", "BPF_JMP|BPF_JGE", False),
	("jle", "BPF_JMP|BPF_JGT", False),
	("jgt", "BPF_JMP|BPF_JGT", True),
	("jge", "BPF_JMP|BPF_JGE", True),
	("jset",  "BPF_JMP|BPF_JSET", True),
	("jnset", "BPF_JMP|BPF_JSET", False),
	("add", "BPF_ALU|BPF_ADD", "04"),
	("sub", "BPF_ALU|BPF_SUB", "04"),
	("mul", "BPF_ALU|BPF_MUL", "04"),
	("div", "BPF_ALU|BPF_DIV", "04"),
	("mod", "BPF_ALU|BPF_MOD", "04"),
	("neg", "BPF_ALU|BPF_NEG", "-"),
	("and", "BPF_ALU|BPF_AND", "04"),
	("or",  "BPF_ALU|BPF_OR",  "04"),
	("xor", "BPF_ALU|BPF_XOR", "04"),
	("lsh", "BPF_ALU|BPF_LSH", "04"),
	("rsh", "BPF_ALU|BPF_RSH", "04"),
	("tax", "BPF_MISC|BPF_TAX", "-"),
	("txa", "BPF_MISC|BPF_TXA", "-"),
	("ret", "BPF_RET", "049"), # docs don't offer 'ret x', but assembler implements it; I'll trust assembler
	]:
	modesup = {}
	ops[op] = modesup
	if isinstance(modes,str):
		for mode in modes:
			for mode2,pattern,add in addrmodes:
				if mode==mode2:
					modesup[pattern] = val+add
	elif modes is None:
		modesup["_"] = None
	else:
		modesup["#_"] = (val,modes)
		modesup["x,#_"] = (val+"|BPF_X",modes) # TODO: check if docs mention this
		modesup["%x,#_"] = (val+"|BPF_X",modes)

#pass 2: tokenize
#after this, opcodes is an array with the following structure:
"""
ld #42
["BPF_LD|BPF_W|BPF_IMM", "42"]
opcode, argument

ldh [I_arch]
["BPF_LD|BPF_H|BPF_K", "I_arch"]

test123:
[":", "test123"]
the colon means it's a label

jeq #__NR_exit, test123
["BPF_JMP|BPF_JEQ", "__NR_exit", "test123", None]
for branches, there are two more fields: target if true and target if false (the other branch goes to None, which is next opcode)

jne #__NR_exit, test123
["BPF_JMP|BPF_JEQ", "__NR_exit", None, "test123"]

jmp test123
[None, None, "test123", "test123"]
unconditional jmp is implemented as brtrue == brfalse; opcode is ignored (hardcoded later)
"""

opcodes = []
for line in bpf.split("\n"):
	line = line.split(";")[0].strip()
	if not line or line[0] == '#': continue
	
	#TODO: label on same line as an opcode?
	if line[-1] == ':':
		line = line[:-1]
		if not re.match(r'[A-Za-z_][A-Za-z0-9_]+', line):
			exit("invalid label name: "+line)
		opcodes.append([":", line])
		continue
	
	op,arg = line.split(None, 1)
	if op not in ops:
		exit("unknown opcode: "+line)
	
	label = None
	if op[0]=='j':
		if op == "jmp":
			opcodes.append([None, "0", arg, arg])
			continue
		
		arg,label = arg.rsplit(",",1)
		arg = arg.strip()
		label = label.strip()
	
	for pattern in ops[op]:
		match = None
		if '_' in pattern:
			left,right = pattern.split("_")
			if arg.startswith(left) and arg.endswith(right):
				if right: match = arg[len(left):-len(right)]
				else: match = arg[len(left):]
		elif arg==pattern:
			match = "0"
		if match:
			opcode = ops[op][pattern]
			if isinstance(opcode,tuple):
				if opcode[1]: opcodes.append([opcode[0], match, label, None])
				else:         opcodes.append([opcode[0], match, None, label])
			else:
				opcodes.append([opcode, match])
			break
	else:
		exit("unknown addressing mode: "+line)


#pass 2.5: check that all referenced labels exist and go forwards
labels = set()
for op in reversed(opcodes):
	if op[0]==':':
		if op[1] in labels:
			exit("duplicate label: "+op[1])
		labels.add(op[1])
	elif len(op)>2:
		if op[2] and op[2] not in labels: exit("unknown or backwards branch: "+op[2])
		if op[3] and op[3] not in labels: exit("unknown or backwards branch: "+op[3])


#third pass: optimize
#  je followed by jmp is merged (making combined je/jne unneeded)
#  unconditional jump to return, or to unconditional jump, is replaced with the target instruction
def is_jmp(op):
	return len(op)==4
def is_jmp_always(op):
	return is_jmp(op) and op[2]==op[3]
again = True
while again:
	again = False
	
	labelfirst = {} # first opcode after a label
	for pos,op in enumerate(opcodes):
		if op[0]==':': labelfirst[op[1]] = opcodes[pos+1]
	
	delete = [False for _ in opcodes]
	for i in range(len(opcodes)-1):
		if is_jmp(opcodes[i]) and is_jmp_always(opcodes[i+1]):
			if not opcodes[i][2]: opcodes[i][2] = opcodes[i+1][2]
			if not opcodes[i][3]: opcodes[i][3] = opcodes[i+1][3]
			delete[i+1] = True
		if is_jmp_always(opcodes[i]):
			target = labelfirst[opcodes[i][2]]
			if is_jmp(target) or 'BPF_RET' in target[0]:
				opcodes[i] = target[:]
				again=True
	
	opcodes = [op for op,rem in zip(opcodes,delete) if not rem]


#fourth pass: generate code, or if branches go out of bounds, deoptimize by splitting out-of-bounds conditional jumps
out = []
again = True
while again:
	labels = {}
	again = False
	out = []
	for op in reversed(opcodes): # since we have only forwards branches, assembling backwards is easier
		if op[0]==':':
			labels[op[1]] = pos
			continue
		
		pos = len(out)
		labels[None] = pos # hacky, but works
		if len(op)>2:
			if op[2]==op[3]:
				line = "BPF_STMT(BPF_JMP|BPF_JA, "+str(pos - labels[op[2]])+"),\n"
			else:
				jt = pos - labels[op[2]]
				jf = pos - labels[op[3]]
				if jt>255 or jf>255:
					#TODO: fix
					#(official assembler seems to just truncate, it seems to be only intended for debugging?)
					exit("jump out of bounds")
				line = "BPF_JUMP("+op[0]+", ("+op[1]+"), "+str(jt)+","+str(jf)+"),\n"
		else:
			line = "BPF_STMT("+op[0]+", ("+op[1]+")),\n"
		out.append(line)

outfile.write("/* Autogenerated, do not edit. All changes will be undone. */\n")
for x in reversed(out): outfile.write(x)
