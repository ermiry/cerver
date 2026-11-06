#!/usr/bin/env python3
# writes a synthetic routes table for benchmarks/bin/tree, the same one for the same arguments
# every route is "METHOD /path": static segments, parameters, shared prefixes,
# several levels of nesting and several methods for the same path
# usage: python3 benchmarks/generate.py <number of routes> [seed] > benchmarks/routes/<name>.txt

import random
import sys

DEFAULT_SEED = 2058

RESOURCES = [
	"users", "items", "orders", "products", "accounts", "sessions", "files", "reports",
	"teams", "projects", "tasks", "comments", "invoices", "payments", "devices", "events"
]

ACTIONS = [
	"update", "status", "history", "export", "summary", "details", "upload", "download"
]

PARAMETERS = [":id", ":name", ":key", ":date"]

METHODS = ["GET", "POST", "PUT", "DELETE", "PATCH"]

MAX_DEPTH = 4
MAX_METHODS = 3

# the matcher of 2.0b-58 has one top level route per service, every other route is below it
PREFIX = "/service"

def route_path (generator):

	segments = []
	for _ in range (generator.randint (1, MAX_DEPTH)):
		segments.append (generator.choice (RESOURCES))
		if generator.random () < 0.6:
			segments.append (generator.choice (PARAMETERS))

	if generator.random () < 0.4:
		segments.append (generator.choice (ACTIONS))

	return PREFIX + "/" + "/".join (segments)

# two paths that only differ in the names of their parameters are the same route
def route_shape (path):

	return "/".join (":" if segment.startswith (":") else segment for segment in path.split ("/"))

def generate (count, seed):

	generator = random.Random (seed)

	routes = [("GET", PREFIX)]
	seen = { ("GET", PREFIX) }
	while len (routes) < count:
		path = route_path (generator)
		for method in generator.sample (METHODS, generator.randint (1, MAX_METHODS)):
			key = (method, route_shape (path))
			if (len (routes) < count) and (key not in seen):
				seen.add (key)
				routes.append ((method, path))

	return routes

def main ():

	if len (sys.argv) < 2:
		sys.exit ("usage: generate.py <number of routes> [seed]")

	count = int (sys.argv[1])
	seed = int (sys.argv[2]) if len (sys.argv) > 2 else DEFAULT_SEED

	for method, path in generate (count, seed):
		print (f"{method} {path}")

if __name__ == "__main__":
	main ()
