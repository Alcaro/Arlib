#!/usr/bin/env python3

#I don't really like bringing in Python, but the alternative is trying to express this in C++ preprocessor,
#which would be ugly both in use (LDA(I_arch) JNE(ARCH_TRUE, die) LABEL(die))
#and implementation (there's no obvious way to represent those labels)
#so this is the best I can do

#this mostly follows the assembly language specified in
# https://www.kernel.org/doc/Documentation/networking/filter.txt
#with a few exceptions:
#  this is an optimizing assembler:
#    je followed by jmp is merged
#      as such, addressing mode 7 (combined je/jne) is not implemented; it's harder to read
#        instead, jnset was added
#    unconditional jump to return is optimized
#    unreachable opcodes are removed
#  je out-of-bounds is converted to jne+jmp (TODO)
#  addressing modes 4 and 8 (immediate constants) pass their argument unchanged to the C compiler, so you can use __NR_exit
#comments are supported via ; (anywhere) or # (start of line only), /* */ not supported
#seccomp-specific docs: https://www.kernel.org/doc/Documentation/prctl/seccomp_filter.txt

#upstream assembler source code:
# http://lxr.free-electrons.com/source/tools/net/bpf_exp.y

#output usage:
#	static const struct sock_filter filter[] = {
#		#include "bpf.inc"
#	};
#	static const struct sock_fprog prog = {
#		.len = (unsigned short)(sizeof(filter)/sizeof(filter[0])),
#		.filter = filter,
#	};
#	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0,0,0)!=0 || prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)!=0)
#		exit(1);

"""
TODO: switch statement
usage:

ld [I_sysno]
switch __NR_%:/usr/include/x86_64-linux-gnu/asm/unistd_64.h sys_%
case open
case read 10
case write 10
case close
default die

sys_open:
sys_read:
sys_write:
sys_close:
die:
ret 0

switch takes the value in A and, if equal to any of the case labels, jumps to that label
if not equal to anything, it jumps to default; default must exist, but can point to the next opcode
this is implemented as a binary tree; it will compare with the middle value, then halve the search space
case values are read from the given filename, as #define name 123 (everything else ignored)
case values in filename, respectively target label names, can also be given; they're pre/suffixed to the case values
the extra argument on cases is how popular that choice is, default 1, to put that branch closer to the root

the above example will expand to something like this (given __NR_read=0, __NR_write=1, __NR_open=2, __NR_close=3):

ld [I_sysno]
jlt 2, switch_0
jeq 0, sys_read
jmp 1, sys_write
switch_0:
jeq 2, sys_open
jeq 3, sys_close
jmp die


to implement:

1
2
3 10
4
6
8
9

cost is number of instructions to reach each target, multiplied by node weight

initial:
<0: =1 =2 =3 =4 =6 =8 =9 (0 5 7 10+)*
asterisk means always true if executed, one opcode less; but real CPUs don't have that instruction, so JIT probably splits it
parens are defaults, weight is 0
cost:
1+2+30+4+5+6+7=55

goal:
<0: =3 <5 =6 =8 =9 (5 7 10+)*
<5: <3 =4 (5)*
<3: =2 =1 (0)*
cost:
1: =3 <5 <3 =2 =1 = 5
2: =3 <5 <3 =2    = 4
3: =3             = 10
4: =3 <5 <3 =4    = 4
6: =3 <5 =6       = 3
8: =3 <5 =6 =8    = 4
9: =3 <5 =6 =8 =9 = 5
= 5+4+10+4+3+4+5 = 35

to reach:
until nothing else helps:
  for every node, if the node gives same results for every value (including default) that can enter, delete it
  for every node, put a < before it, comparing to the bisection of the reachable costs
  for every less-than node, move it one step down, or one step up
  for every non-root node, move it one step higher
  if either of these found a cheaper tree, keep that

"""

def assemble(bpf):
	def die(why):
		exit(why)
	
	#"pass" 1: compile list of opcodes and addressing modes
	#output: ops = { "ld": { "M[_]": "BPF_LD|BPF_W|BPF_MEM" } }
	import re
	addrmodes = [
		('-', "", ""),
		('0', "x", "|BPF_X"),
		('0', "%x", "|BPF_X"),
		('1', "[_]", "|BPF_ABS"),
		('2', "[x+_]", "|BPF_IND"),
		('3', "M[_]", "|BPF_MEM"), # BPF_MEM is documented as scratch space, probably useless for seccomp
		('E', "M[_]", ""), # turns out BPF_ST doesn't use BPF_MEM; E is almost-3
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
		("ldi", "BPF_LD|BPF_W", "4"), # the linux bpf assembler allows this without #, as an undocumented extension; I don't
		("ldh", "BPF_LD|BPF_H", "12"),
		("ldb", "BPF_LD|BPF_B", "12"),
		("ldx",  "BPF_LDX|BPF_W", "345"),
		("ldxi", "BPF_LDX|BPF_W", "4"),
		("ldxb", "BPF_LDX|BPF_B", "5"),
		("st",  "BPF_ST", "E"),
		("stx", "BPF_STX", "E"),
		("jmp", "BPF_JMP|BPF_JA",  None), # for branches, the third param is whether the label goes to True or False
		("jeq", "BPF_JMP|BPF_JEQ", True), # the other falls through
		("jne", "BPF_JMP|BPF_JEQ", False), # JMP goes to both, so None
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
	[["#0"], "BPF_LD|BPF_W|BPF_IMM", "42", "#1"]
	labels, opcode, argument, next
	every opcode has an implicit label named after its opcode ID, and implicitly point to next opcode
	
	ret #0
	[["#0"], "BPF_RET|BPF_IMM", "0"]
	except return, of course
	
	ldh [I_arch]
	[["#1"], "BPF_LD|BPF_H|BPF_K", "I_arch", "#2"]
	
	test123: ld #123
	[["#1", "test123"], "BPF_LD|BPF_W|BPF_IMM", "123", "#2"]
	
	jeq #__NR_exit, test123
	[["#0"], "BPF_JMP|BPF_JEQ", "__NR_exit", "test123", "#1"]
	branches have two targets: target if true and target if false
	the branch taken is hardcoded
	
	jne #__NR_exit, test123
	[["#0"], "BPF_JMP|BPF_JEQ", "__NR_exit", "#1", "test123"]
	
	jmp test123
	[["#0"], None, "0", "test123", "test123"]
	unconditional jmp is implemented as true == false; opcode is ignored (hardcoded later)
	"""
	
	opcodes = []
	labelshere = []
	for line in bpf.split("\n"):
		line = line.split(";")[0].strip()
		
		m = re.match(r'([A-Za-z_][A-Za-z0-9_]*):(.*)$', line)
		if m: # I'd make this a while, but Python doesn't like while x=foo():.
			labelshere.append(m.group(1))
			line = m.group(2).strip()
		
		if not line or line[0] == '#': continue
		
		labelshere.append("#"+str(len(opcodes)))
		nextlabel = "#"+str(len(opcodes)+1)
		
		parts = line.split(None, 1)
		if len(parts)==1: parts+=[""]
		op,arg = parts
		if op not in ops:
			die("unknown opcode: "+line)
		
		label = None
		if op[0]=='j':
			if op == "jmp":
				opcodes.append([labelshere, None, "0", arg, arg])
				labelshere = []
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
					if opcode[1]: opcodes.append([labelshere, opcode[0], match, label, nextlabel])
					else:         opcodes.append([labelshere, opcode[0], match, nextlabel, label])
				elif 'BPF_RET' in opcode:
					opcodes.append([labelshere, opcode, match])
				else:
					opcodes.append([labelshere, opcode, match, nextlabel])
				break
		else:
			die("unknown addressing mode: "+line)
		labelshere = []
	#if labelshere:
		#die("label points nowhere: "+' '.join(labelshere))
	
	
	#pass 2.5: check that all referenced labels exist and go forwards
	labels = set()
	for op in reversed(opcodes):
		for l in op[0]:
			if l in labels:
				die("duplicate label: "+l)
			labels.add(l)
		if len(op)>3 and op[3] not in labels: die("unknown or backwards branch: "+op[3])
		if len(op)>4 and op[4] not in labels: die("unknown or backwards branch: "+op[4])
	
	
	#third pass: optimize
	def is_jmp(op):
		return len(op)==5
	def is_jmp_always(op):
		return is_jmp(op) and op[3]==op[4]
	again = True
	#again = False # uncomment to disable this pass, for debugging
	while again:
		again = False
		
		labels_used = set()
		labels_used.add("#0") # the entry point is always reachable
		label_op = {} # opcode containing a label
		for pos,op in enumerate(opcodes):
			for l in op[0]:
				label_op[l] = op
			if len(op)>3: labels_used.add(op[3])
			if len(op)>4: labels_used.add(op[4])
		
		delete = [False for _ in opcodes]
		for i in range(len(opcodes)):
			#label isn't used -> remove
			opcodes[i][0] = [l for l in opcodes[i][0] if l in labels_used]
			
			#opcode has no labels -> unreachable -> delete
			if not opcodes[i][0]:
				delete[i] = True
				again = True
			
			#conditional jump to unconditional (including implicit) -> replace target
			if is_jmp(opcodes[i]):
				target1 = label_op[opcodes[i][3]]
				if is_jmp_always(target1):
					opcodes[i][3] = target1[3]
					again=True
				target2 = label_op[opcodes[i][4]]
				if is_jmp_always(target2):
					opcodes[i][4] = target2[4]
					again=True
			
			if is_jmp_always(opcodes[i]):
				#unconditional jump to return or unconditional jump -> flatten
				target = label_op[opcodes[i][3]]
				if is_jmp_always(target) or 'BPF_RET' in target[1]:
					newop = target[:]
					newop[0] = opcodes[i][0]
					opcodes[i] = newop
					again=True
				#unconditional jump to next -> delete
				if target is opcodes[i+1]:
					delete[i] = True
					opcodes[i+1][0] += opcodes[i][0]
					again = True
		
		opcodes = [op for op,rem in zip(opcodes,delete) if not rem]
	
	#fourth pass: generate code, or if branches go out of bounds, deoptimize by splitting out-of-bounds conditional jumps
	out = []
	again = True
	while again:
		labels = {}
		again = False
		out = []
		for op in reversed(opcodes): # since we have only forwards branches, assembling backwards is easier
			pos = len(out)
			for l in op[0]: labels[l] = pos
			pos -= 1 # -1 because jmp(0) goes to the next opcode, not the same one again
			
			if len(op)>4:
				if op[3]==op[4]:
					line = "BPF_STMT(BPF_JMP|BPF_JA, "+str(pos - labels[op[3]])+"),\n"
				else:
					jt = pos - labels[op[3]]
					jf = pos - labels[op[4]]
					if jt>255 or jf>255:
						#TODO: fix
						#(official assembler seems to just truncate, it seems to be only intended for debugging?)
						die("jump out of bounds")
					line = "BPF_JUMP("+op[1]+", ("+op[2]+"), "+str(jt)+","+str(jf)+"),\n"
			else:
				line = "BPF_STMT("+op[1]+", ("+op[2]+")),\n"
			out.append(line)
	
	if len(out)>65535:
		die("can't fit "+str(len(out))+" instructions in struct sock_fprog")
	return "".join(reversed(out))


def testsuite(silent):
	def test(bpf, exp):
		act = assemble(bpf.replace(";", "\n")).strip().replace("\n", " ")
		if act==exp:
			if not silent:
				print("input:", bpf)
				print("pass")
				print()
		else:
			print("input:", bpf)
			print("expected:", exp)
			print("actual:  ", act)
			print()
	#ensure jeq+jmp is merged, and jmp is killed as dead code
	test("jeq #0, ok; jmp die; ok:; ret #0; die:; ret #1",
	     "BPF_JUMP(BPF_JMP|BPF_JEQ, (0), 0,1), BPF_STMT(BPF_RET|BPF_IMM, (0)), BPF_STMT(BPF_RET|BPF_IMM, (1)),")
	#ensure jump to return is optimized
	test("jmp die; ok:; ret #0; die:; ret #1",
	     "BPF_STMT(BPF_RET|BPF_IMM, (1)),")
	#ensure jeq to jmp is flattened
	test("jeq #0, c; jeq #0, a; jmp b; c:; ret #2; a:; jmp a2; b:; jmp b2; a2:; ld [0]; ret #0; b2:; ld [1]; ret #1",
	     "BPF_JUMP(BPF_JMP|BPF_JEQ, (0), 1,0), "+ # jeq #0, c
	     "BPF_JUMP(BPF_JMP|BPF_JEQ, (0), 1,3), "+ # jeq #0, a; jmp b; a: jmp a2; b: jmp b2
	     "BPF_STMT(BPF_RET|BPF_IMM, (2)), "+      # c: ret #2
	     "BPF_STMT(BPF_LD|BPF_W|BPF_ABS, (0)), "+ # a2: ld [0]
	     "BPF_STMT(BPF_RET|BPF_IMM, (0)), "+      # ret #0
	     "BPF_STMT(BPF_LD|BPF_W|BPF_ABS, (1)), "+ # b2: ld [1]
	     "BPF_STMT(BPF_RET|BPF_IMM, (1)),")       # ret #1
	#ensure labels don't need to be on their own line
	test("jmp x; x: ret #0", "BPF_STMT(BPF_RET|BPF_IMM, (0)),")
	#ensure this opcode assembles properly
	test("txa; ret #0", "BPF_STMT(BPF_MISC|BPF_TXA, (0)), BPF_STMT(BPF_RET|BPF_IMM, (0)),")
testsuite(True)


import os, sys

if len(sys.argv)==1:
	bpf = sys.stdin.read()
	outfile = lambda: sys.stdout # lambdas to ensure file isn't created on failure
if len(sys.argv)==2:
	if sys.argv[1]=='--test':
		#testsuite(False)
		exit(0)
	
	bpf = open(sys.argv[1], "rt").read()
	outfile = lambda: open(os.path.splitext(sys.argv[1])[0]+".inc", "wt")
	sys.stdout
if len(sys.argv)==3:
	bpf = open(sys.argv[1], "rt").read()
	outfile = lambda: open(sys.argv[2], "wt")

bpfbin = assemble(bpf)

outfile = outfile()
outfile.write("/* Autogenerated, do not edit. All changes will be undone. */\n")
outfile.write(bpfbin)
