
#include <stdlib.h>

#include <QtDebug>

#include "flow.h"

#include "main.h"
#include "seg.h"

using namespace SST;


#define FLOW_PORT	NETSTERIA_DEFAULT_PORT


SegTest::SegTest()
:	h1(&sim), h2(&sim), h3(&sim), h4(&sim),
	fs1(&h1, QHostAddress("1.1.34.4")),
	fs4(&h4, QHostAddress("1.1.12.1")),
	fr1(&h1), fr2(&h2), fr3(&h3), fr4(&h4),
	cli(&h1),
	srv(&h4),
	srvs(NULL)
{
	// Set up the linear topology
	h1.attach(QHostAddress("1.1.12.1"), &l12);
	h2.attach(QHostAddress("1.1.12.2"), &l12);
	h2.attach(QHostAddress("1.1.23.2"), &l23);
	h3.attach(QHostAddress("1.1.23.3"), &l23);
	h3.attach(QHostAddress("1.1.34.3"), &l34);
	h4.attach(QHostAddress("1.1.34.4"), &l34);

	// Set up the flow layer forwarding chain (XXX horrible hack)
	fr2.forwardTo(Endpoint(QHostAddress("1.1.23.3"), FLOW_PORT));
	fr3.forwardTo(Endpoint(QHostAddress("1.1.34.4"), FLOW_PORT));
	fr4.forwardUp(&fs4);

	// Initiate a flow layer connection
	fs1.initiateTo(Endpoint(QHostAddress("1.1.12.2"), FLOW_PORT));

	// Set up the application-level server
	connect(&srv, SIGNAL(newConnection()),
		this, SLOT(gotConnection()));
	if (!srv.listen("regress", "SST regression test server",
			"migrate", "Migration test protocol"))
		qFatal("Can't listen on service name");

	// Open a connection to the server
	connect(&cli, SIGNAL(readyReadMessage()), this, SLOT(gotMessage()));
	cli.connectTo(h4.hostIdent(), "regress", "seg");
	cli.connectAt(Endpoint(QHostAddress("1.1.34.4"), NETSTERIA_DEFAULT_PORT));

	// Get the ping-pong process going...
	ping(&cli);
}

void SegTest::gotConnection()
{
	qDebug() << this << "gotConnection";
	Q_ASSERT(srvs == NULL);

	srvs = srv.accept();
	if (!srvs) return;

	srvs->listen(Stream::Unlimited);

	connect(srvs, SIGNAL(readyReadMessage()), this, SLOT(gotMessage()));
}

void SegTest::ping(Stream *strm)
{
	// Send a random-size message
	int p2 = 22; 	// lrand48() % 16;
	QByteArray buf;
	buf.resize(1 << p2);
	//qDebug() << strm << "send msg size" << buf.size();
	strm->writeMessage(buf);
}

void SegTest::gotMessage()
{
	Stream *strm = (Stream*)QObject::sender();
	while (true) {
		QByteArray buf = strm->readMessage();
		if (buf.isNull())
			return;

		qDebug() << strm << "recv msg size" << buf.size();

#if 0
		narrived++;

		if (narrived == MSGPERIOD) {
			timeperiod = clihost.currentTime().usecs
				- starttime.usecs;
			qDebug() << "migr" << nmigrates << "timeperiod:"
				<< (double)timeperiod / 1000000.0;

			// Wait some random addional time before migrating,
			// to ensure that migration can happen at
			// "unexpected moments"...
			migrater.start(timeperiod * drand48());
		}

		// Keep ping-ponging until we're done.
		if (nmigrates < MAXMIGRS)
			ping(strm);
#endif
	}
}

void SegTest::run()
{
	SegTest test;
	test.sim.run();
	success = true;
}

