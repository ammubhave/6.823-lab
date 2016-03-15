#!/usr/bin/python

import os
import sys

if len(sys.argv) != 2:
	raise Exception('You need to pass in the results directory as argument')

results_dir = sys.argv[1]

files = [
	'bzip2_base.gcc4-high-opt___input.random___2.out',
	'cc1_base.gcc4-high-opt___cccp.i___-o___foo.o.out',
    'crafty_base.gcc4-high-opt_______crafty.in.out',
    'gap_base.gcc4-high-opt___-l___input___-q___-m___64M_______test.in.out',
    'gzip_base.gcc4-high-opt___input.compressed___2.out',
    'parser_base.gcc4-high-opt___2.1.dict___-batch_______test.in.out',
    'applu_base.gcc4-high-opt_______applu.in.out',
    'art_base.gcc4-high-opt___-scanfile___c756hel.in___-trainfile1___a10.img___-stride___2___-startx___134___-starty___220___-endx___139___-endy___225___-objects___1.out',
    'equake_base.gcc4-high-opt_______inp.in.out',
    'mesa_base.gcc4-high-opt___-frames___10___-meshfile___mesa.in.out',
    'swim_base.gcc4-high-opt_______swim.in.out']

takenCorrects = []
takenIncorrects = []
notTakenCorrects = []
notTakenIncorrects = []
for fname in files:
	with open(os.path.join(results_dir, fname)) as f:
		s = f.read()
		s = s.rstrip('\n')
		#print s.split(' ')
		(_, takenCorrect, _, _, takenIncorrect, _, notTakenCorrect, _, notTakenIncorrect) = tuple(s.split(' '))
		takenCorrects.append(takenCorrect)
		takenIncorrects.append(takenIncorrect)
		notTakenCorrects.append(notTakenCorrect)
		notTakenIncorrects.append(notTakenIncorrect)

takenCorrects = takenCorrects[:6] + ['',''] + takenCorrects[6:]
takenIncorrects = takenIncorrects[:6] + ['',''] + takenIncorrects[6:]
notTakenCorrects = notTakenCorrects[:6] + ['',''] + notTakenCorrects[6:]
notTakenIncorrects = notTakenIncorrects[:6] + ['',''] + notTakenIncorrects[6:]

final_list = zip(takenCorrects, takenIncorrects, notTakenCorrects, notTakenIncorrects)
print '\n'.join(map('\t'.join, final_list))

# print '\n'.join(takenCorrects)
# print
# print '\n'.join(takenIncorrects)
# print
# print '\n'.join(notTakenCorrects)
# print
# print '\n'.join(notTakenIncorrects)
