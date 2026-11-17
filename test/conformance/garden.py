#!/usr/bin/env python3
# the HTTP Garden against cerver (test/conformance/garden.sh): every case of garden.txt goes to every origin that
# runs, and the origins that cerver does not agree with must be the ones that the file says
# usage: garden.py <garden.txt> [check | record | fuzz <iterations>]
#   check   fails on a case whose origins are not the ones of the file
#   record  prints the cases with the origins of this run, to write the file
#   fuzz    the mutations of the Garden over the cases: prints each one where cerver is alone against the others

import codecs
import os
import random
import sys

sys.path.insert (0, os.environ.get ("GARDEN_TOOLS", "/garden/tools"))

from diff import ErrorType
from fanout import fanout
from grid import generate_grid
from mutations import mutate
from targets import ORIGIN_DICT

CERVER = "cerver"

# a case: its name, the origins that cerver does not agree with ("-" for none) and the payload with its escapes
def read_cases (path):

	cases = []
	with open (path, encoding = "latin1") as cases_file:
		for line in cases_file:
			line = line.rstrip ("\n")
			if not line or line.startswith ("#"):
				continue

			name, expected, payload = line.split ("\t", 2)
			data = codecs.escape_decode (payload.encode ("latin1"))[0]
			cases.append ((name, set () if expected == "-" else set (expected.split (",")), payload, data))

	return cases

def origins ():

	servers = [ORIGIN_DICT[name] for name in sorted (ORIGIN_DICT)]
	if CERVER not in ORIGIN_DICT:
		sys.exit ("the origin cerver does not run")

	return servers

# the origins that cerver does not agree with, with the kind of each discrepancy
def disagreements (data, servers):

	results = fanout ([data], servers)
	grid = generate_grid (results, servers)

	index = [server.name for server in servers].index (CERVER)

	found = {}
	for other, server in enumerate (servers):
		if other == index:
			continue

		entry = grid[min (index, other)][max (index, other)]
		if entry not in (ErrorType.OK, None):
			found[server.name] = entry.name

	return found

def check (cases, servers):

	failed = 0
	for name, expected, payload, data in cases:
		found = disagreements (data, servers)

		# an origin that does not run in this run is not compared
		running = set (server.name for server in servers)
		expected = expected & running

		if set (found) != expected:
			failed += 1
			print ("FAILED %s: cerver does not agree with %s, the file says %s" % (
				name, sorted (found.items ()) or "-", sorted (expected) or "-"
			))

	print ("cases %d - failed %d - origins %s" % (len (cases), failed, " ".join (server.name for server in servers)))

	return failed

def record (cases, servers):

	for name, expected, payload, data in cases:
		found = disagreements (data, servers)
		print ("%s\t%s\t%s" % (name, ",".join (sorted (found)) or "-", payload))
		if found:
			print ("#\t%s" % " ".join ("%s=%s" % item for item in sorted (found.items ())))

	return 0

# the mutations where cerver is alone: every other origin agrees with the rest and none with cerver
def fuzz (cases, servers, iterations):

	others = [server.name for server in servers if server.name != CERVER]

	random.seed (int (os.environ.get ("GARDEN_SEED", "0")))

	alone = 0
	for i in range (iterations):
		name, expected, payload, data = random.choice (cases)

		# a mutation of the Garden parses its seed with its own parser, that does not take every case
		try:
			mutated = mutate ([data])
		except Exception:
			continue

		results = fanout (mutated, servers)
		grid = generate_grid (results, servers)
		names = [server.name for server in servers]
		index = names.index (CERVER)

		def agree (a, b):
			return grid[min (a, b)][max (a, b)] == ErrorType.OK

		against = [names[j] for j in range (len (names)) if (j != index) and not agree (index, j)]
		together = all (
			agree (names.index (a), names.index (b)) for a in others for b in others if a < b
		)

		if together and (len (against) == len (others)):
			alone += 1
			print ("ALONE %r (from %s)" % (b"".join (mutated), name), flush = True)

	print ("mutations %d - cerver alone %d" % (iterations, alone))

	return 0

def main ():

	cases = read_cases (sys.argv[1])
	mode = sys.argv[2] if len (sys.argv) > 2 else "check"

	servers = origins ()

	if mode == "record":
		code = record (cases, servers)
	elif mode == "fuzz":
		code = fuzz (cases, servers, int (sys.argv[3]))
	else:
		code = 1 if check (cases, servers) else 0

	sys.exit (code)

if __name__ == "__main__":
	main ()
