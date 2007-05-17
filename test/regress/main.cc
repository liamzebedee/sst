
#include <string.h>
#include <stdlib.h>

#include <QHostInfo>
#include <QHostAddress>
#include <QCoreApplication>
#include <QtDebug>

#include "sock.h"
#include "host.h"

#include "main.h"
#include "cli.h"
#include "srv.h"
#include "sim.h"
#include "dgram.h"
#include "migrate.h"

using namespace SST;


QHostAddress SST::cliaddr("1.2.3.4");
QHostAddress SST::srvaddr("4.3.2.1");

bool SST::success;
int SST::nerrors;


struct RegressionTest {
	void (*run)();
	const char *name;
	const char *descr;
} tests[] = {
	{BasicClient::run, "basic", "Basic stream-oriented data transfer"},
	{DatagramTest::run, "dgram", "Best-effort datagram data transfer"},
	{MigrateTest::run, "migrate", "Endpoint migration test"},
};
#define NTESTS ((int)(sizeof(tests)/sizeof(tests[0])))

void usage(const char *appname)
{
	fprintf(stderr, "Usage: %s [<testname>]\n"
			"Tests:\n", appname);
	for (int i = 0; i < NTESTS; i++)
		fprintf(stderr, "  %-10s %s\n", tests[i].name, tests[i].descr);
	fprintf(stderr, "Runs all tests if invoked without an argument.\n");
	exit(1);
}

bool runtest(int n)
{
	RegressionTest &t = tests[n];

	printf("Running test '%s': ", t.name);
	fflush(stdout);

	success = false;
	nerrors = 0;

	t.run();

	bool succ = success && (nerrors == 0);
	printf(succ ? "passed\n" : "FAILED\n");

	return succ;
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);

	if (argc == 1) {

		// Just run all tests in succession.
		printf("Running all %d regression tests:\n", NTESTS);
		int nfail = 0;
		for (int i = 0; i < NTESTS; i++) {
			if (!runtest(i))
				nfail++;
		}

		// Print summary of results
		if (nfail) {
			printf("%d of %d tests FAILED\n", nfail, NTESTS);
			exit(1);
		} else
			printf("All tests passed.\n");

	} else if (argc == 2) {

		// Find and run the named test.
		for (int i = 0; ; i++) {
			if (i >= NTESTS)
				usage(argv[0]);	// Test not found
			if (strcasecmp(argv[1], tests[i].name) != 0)
				continue;

			// Run the test
			if (!runtest(i))
				exit(1);
			break;
		}

	} else
		usage(argv[0]);

	return 0;
}

