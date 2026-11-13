import json
import sys

PASSED = {"OK", "NON-STRICT", "INFORMATIONAL"}

# every case of autobahn-testsuite 25.10.1: a report with fewer of them was cut,
# the cases after a connection that failed are not in it
CASES = 517

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

	print ("%s: %d cases of %d, %d failed" % (path, cases, CASES, len (failures)))
	for failure in sorted (failures):
		print ("\t" + failure)

	return (cases == CASES) and not failures

def main ():

	passed = True
	for path in sys.argv[1:]:
		passed = check (path) and passed

	sys.exit (0 if passed else 1)

if __name__ == "__main__":
	main ()
