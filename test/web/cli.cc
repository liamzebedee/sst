
#include <netinet/in.h>

#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QScrollBar>
#include <QToolBar>
#include <QTextEdit>
#include <QTextDocument>
#include <QUrl>

#include "cli.h"

using namespace SST;


WebClient::WebClient(Host *host, const QHostAddress &srvaddr, int srvport,
			Simulator *sim)
:	host(host), srvaddr(srvaddr), srvport(srvport), sim(sim),
	refresher(host)
{
	resize(1000, 700);

	QToolBar *toolbar = addToolBar(tr("Browser Controls"));
	toolbar->addAction(QIcon("images/forward.png"), tr("Forward")); 
	toolbar->addAction(QIcon("images/back.png"), tr("Back"));
	toolbar->addAction(QIcon("images/reload.png"), tr("Reload"),
						this, SLOT(reload()));

	priocheck = new QCheckBox(tr("Prioritize Loads"), this);
	toolbar->addWidget(priocheck);
	connect(priocheck, SIGNAL(stateChanged(int)),
		this, SLOT(refreshSoon()));

	// We only get a speed-throttle control on the simulated network.
	if (sim) {
		QSlider *speedslider = new QSlider(this);
		speedslider->setMinimum(10);	// 2^10 = 1024 bytes per sec
		speedslider->setMaximum(30);	// 2^30 = 1 GByte per sec
		speedslider->setValue(14);	// 2^14 = 16KB/sec
		speedslider->setOrientation(Qt::Horizontal);
		toolbar->addWidget(speedslider);

		speedlabel = new QLabel(this);
		toolbar->addWidget(speedlabel);

		connect(speedslider, SIGNAL(valueChanged(int)),
			this, SLOT(speedSliderChanged(int)));
		speedSliderChanged(speedslider->value());
	}

	QFile pagefile("page/index.html");
	if (!pagefile.open(QIODevice::ReadOnly))
		qFatal("can't open web page");
	QString html = QString::fromAscii(pagefile.readAll());

	textedit = new QTextEdit(this);
	textedit->setReadOnly(true);

	QTextDocument *doc = textedit->document();
	doc->setHtml(html);

	// Find all the images in the web page, and set their true sizes.
	// This cheat is just because I'm too lazy to add width=, height=
	// fields to all the img markups in my photo album pages...
	QTextCursor curs(doc);
	QString imgchar = QString(QChar(0xfffc));
	while (true) {
		curs = doc->find(imgchar, curs);
		if (curs.isNull())
			break;

		// Get the image's format information
		QTextFormat fmt = curs.charFormat();
		if (!fmt.isImageFormat()) {
			qDebug() << "image character isn't image!?";
			continue;
		}
		QTextImageFormat ifmt = fmt.toImageFormat();
		//qDebug() << "image" << ifmt.name() << "width" << ifmt.width()
		//		<< "height" << ifmt.height();
		QString name = ifmt.name();

		// Look at the actual image file to get the image's size.
		// (This is where the horrible cheat occurs.)
		QString pagename = "page/" + name;
		QImage img(pagename);
		if (img.isNull()) {
			qDebug() << "couldn't find" << ifmt.name();
			continue;
		}
		ifmt.setWidth(img.width());
		ifmt.setHeight(img.height());
		//ifmt.setName("images/blank.png");
		//ifmt.setName("page/" + ifmt.name());
		curs.setCharFormat(ifmt);

		// Record the name and text position of this image.
		WebImage wi;
		wi.curs = curs;
		wi.name = ifmt.name();
		wi.imgsize = QFile(pagename).size();
		images.append(wi);
	}

#if 0
	// Now that all the image sizes are known
	// and the document is hopefully formatted correctly,
	// find where all the images appear in the viewport.
	for (int i = 0; i < images.size(); i++) {
		WebImage &wi = images[i];
		QTextCursor curs(doc);

		curs.setPosition(wi.curs.selectionStart());
		QRect rbef = textedit->cursorRect(curs);
		curs.setPosition(wi.curs.selectionEnd());
		QRect raft = textedit->cursorRect(curs);
		wi.rect = rbef.united(raft);
		qDebug() << "image" << i << "rect" << wi.rect;
	}
#endif

	textedit->moveCursor(QTextCursor::Start);

	setCentralWidget(textedit);

	// Watch the text edit box's vertical scroll bar for changes.
	connect(textedit->verticalScrollBar(), SIGNAL(valueChanged(int)),
		this, SLOT(refreshSoon()));

	connect(&refresher, SIGNAL(timeout(bool)), this, SLOT(refreshNow()));

	// Start loading the images on this page
	reload();
}

void WebClient::reload()
{
	// Open a stream and send a request for each image.
	for (int i = 0; i < images.size(); i++) {
		WebImage &wi = images[i];

		// Delete any previous stream and temp file
		if (wi.strm) {
			Q_ASSERT(strms.value(wi.strm) == i);
			strms.remove(wi.strm);
			wi.strm->shutdown(Stream::Reset);
			wi.strm->deleteLater();
			wi.strm = NULL;
		}
		if (wi.tmpf) {
			wi.tmpf->remove();
			delete wi.tmpf;
			wi.tmpf = NULL;
		}

		// Reset the visible image to the special blank image
		QTextImageFormat ifmt = wi.curs.charFormat().toImageFormat();
		ifmt.setName("images/blank.png");
		wi.curs.setCharFormat(ifmt);

		// Create a temporary file to load the image into
		wi.tmpf = new QFile("tmp/" + wi.name, this);
		QDir().mkpath(wi.tmpf->fileName());
		QDir().rmdir(wi.tmpf->fileName());
		if (!wi.tmpf->open(QIODevice::WriteOnly | QIODevice::Truncate))
			qFatal("Can't write tmp image");

		// Create an SST stream on which to download the image
		wi.strm = new Stream(host, this);
		connect(wi.strm, SIGNAL(readyRead()), this, SLOT(readyRead()));
		strms.insert(wi.strm, i);

		// Connect to the server and send the request
		wi.strm->connectTo(Ident::fromIpAddress(srvaddr, srvport),
					"webtest", "basic");
		wi.strm->writeMessage(wi.name.toAscii());
	}

	refreshSoon();
}

void WebClient::setPriorities()
{
	//qDebug() << "setPriorities";

	if (!priocheck->isChecked()) {
		// Act like HTTP/1.1 with no prioritization -
		// we do this simply by giving early requests
		// higher priority than late requests, in pairs,
		// to mimic HTTP/1.1 with two concurrent TCP streams.
		for (int i = 0; i < images.size(); i++)
			setPri(i, -i/2);
		return;
	}

	int start = textedit->cursorForPosition(QPoint(0,0)).position();
	int end = textedit->cursorForPosition(
				QPoint(textedit->width(),textedit->height()))
			.position();
	qDebug() << "window:" << start << "to" << end;

	for (int i = 0; i < images.size(); i++) {
		WebImage &wi = images[i];

		// Determine if this image is currently visible
		bool vis = wi.curs.selectionStart() <= end &&
				wi.curs.selectionEnd() >= start;

		//QTextImageFormat ifmt = wi.curs.charFormat().toImageFormat();
		//ifmt.setName(vis ? "images/black.png" : "images/blank.png");
		//wi.curs.setCharFormat(ifmt);

		// Set the image's priority accordingly
		setPri(i, vis ? 1 : 0);
	}
}

void WebClient::setPri(int i, int pri)
{
	WebImage &wi = images[i];
	if (wi.strm == NULL || wi.pri == pri)
		return;		// no change

	// Send a priority change message
	quint32 msg = htonl(pri);
#if 0
	Stream *sub = wi.strm->openSubstream();
	sub->writeMessage((char*)&msg, 4);
	sub->deleteLater();
#endif
	wi.strm->writeDatagram((char*)&msg, sizeof(msg), true);

	wi.pri = pri;
}

void WebClient::readyRead()
{
	Stream *strm = (Stream*)sender();
	Q_ASSERT(strms.contains(strm));
	int win = strms.value(strm);
	WebImage &wi = images[win];
	Q_ASSERT(wi.strm == strm);

	while (true) {
		QByteArray buf;
		buf.resize(wi.strm->bytesAvailable());
		int act = wi.strm->readData(buf.data(), buf.size());
		if (act <= 0) {
			if (wi.strm->atEnd()) {
				// Done loading this image!
				done:
				qDebug() << "img" << win << "done";
				wi.strm->deleteLater();
				wi.strm = NULL;
			}
			return;
		}

		// Write the image data to the temporary file
		wi.tmpf->write(buf, act);
		wi.tmpf->flush();

		qDebug() << "image" << win << "got" << wi.tmpf->pos()
			<< "of" << wi.imgsize << "bytes";

		// Update the image in the browser window ASAP
		wi.dirty = true;
		refreshSoon();

		// XX shouldn't be necessary!
		if (wi.tmpf->pos() >= wi.imgsize)
			goto done;
	}
}

void WebClient::refreshSoon()
{
	if (!refresher.isActive())
		refresher.start(100*1000);	 // 1/10th sec
}

void WebClient::refreshNow()
{
	//qDebug() << "refresh";
	refresher.stop();

	// Send priority change requests
	setPriorities();

	// Update on-screen images
	for (int i = 0; i < images.size(); i++) {
		WebImage &wi = images[i];

		if (!wi.dirty)
			continue;
		wi.dirty = false;

		// Load the image from disk
		QImage img("tmp/" + wi.name);
		textedit->document()->addResource(QTextDocument::ImageResource,
						QUrl(wi.name), img);
		QTextImageFormat ifmt = wi.curs.charFormat().toImageFormat();
		ifmt.setName(wi.name);
		wi.curs.setCharFormat(ifmt);
	}
}

void WebClient::speedSliderChanged(int value)
{
	Q_ASSERT(value >= 0 && value <= 30);
	int bw = 1 << value;

	qDebug() << "change network bandwidth:" << bw;

	sim->setNetRate(bw);

	QString str;
	if (value >= 30)
		str = tr("%0 GBytes/sec").arg(1 << (value-30));
	else if (value >= 20)
		str = tr("%0 MBytes/sec").arg(1 << (value-20));
	else if (value >= 10)
		str = tr("%0 KBytes/sec").arg(1 << (value-10));
	else
		str = tr("%0 Bytes/sec").arg(1 << value);
	speedlabel->setText(str);
}

