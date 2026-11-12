import json
import sys

PASSED = {"OK", "NON-STRICT", "INFORMATIONAL"}

def check (path):

	with open (path) as index_file:
		index = json.load (index_file)

	failures = []
	cases = 0
	for agent, results in index.items ():
		for case, result in results.items ():
			cases += 1
			behavior = result.get ("behavior")
			close = result.get ("behaviorClose")
			if (behavior not in PASSED) or (close not in PASSED):
				failures.append ("%s %s: %s / %s" % (agent, case, behavior, close))

	print ("%s: %d cases, %d failed" % (path, cases, len (failures)))
	for failure in sorted (failures):
		print ("\t" + failure)

	return (cases > 0) and not failures

def main ():

	passed = True
	for path in sys.argv[1:]:
		passed = check (path) and passed

	sys.exit (0 if passed else 1)

if __name__ == "__main__":
	main ()
