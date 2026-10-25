/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pdfpreview.h"
#include "base/ProcessUtil.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {
	constexpr int minPageWidthCss = 480;
	constexpr int maxPageWidthCss = 1400;

	constexpr double supersample = 1.25;

	constexpr double a4WidthPt = 595.0;

	int pageNumberOf(const QString &fileName) {
		const QString base = QFileInfo(fileName).completeBaseName();
		const int dash = base.lastIndexOf(QChar('-'));

		if (dash < 0)
			return 0;

		bool ok = false;
		const int number = base.mid(dash + 1).toInt(&ok);
		return ok ? number : 0;
	}
}

PdfPreviewWidget::PdfPreviewWidget(QWidget *parent) : QWidget(parent) {
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	auto *header = new QHBoxLayout();
	header->addWidget(new QLabel(tr("PDF Preview"), this));
	header->addStretch();
	auto *refreshButton = new QPushButton(tr("Refresh"), this);
	header->addWidget(refreshButton);
	layout->addLayout(header);

	scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
	pagesWidget = new QWidget(scroll);
	pagesLayout = new QVBoxLayout(pagesWidget);
	pagesLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
	scroll->setWidget(pagesWidget);
	layout->addWidget(scroll, 1);

	messageLabel = new QLabel(this);
	messageLabel->setAlignment(Qt::AlignCenter);
	messageLabel->setWordWrap(true);
	pagesLayout->addWidget(messageLabel, 0, Qt::AlignHCenter);

	infoLabel = new QLabel(this);
	layout->addWidget(infoLabel);

	connect(refreshButton, &QPushButton::clicked, this, [this] { render(); });

	renderTimer = new QTimer(this);
	renderTimer->setSingleShot(true);
	renderTimer->setInterval(300);
	connect(renderTimer, &QTimer::timeout, this, [this] { render(); });

	setMessage(tr("Compile the statement to see the preview here."));
}

PdfPreviewWidget::~PdfPreviewWidget() { abortRender(); }

void PdfPreviewWidget::scheduleRender() {
	if (renderTimer)
		renderTimer->start();
}

void PdfPreviewWidget::clearPages() {
	for (QLabel *label : pageLabels) {
		pagesLayout->removeWidget(label);
		label->hide();
		label->deleteLater();
	}

	pageLabels.clear();
	messageLabel->clear();
}

void PdfPreviewWidget::clear() {
	clearPages();
	pdfPath.clear();
	infoLabel->clear();
	setMessage(tr("Compile the statement to see the preview here."));
}

void PdfPreviewWidget::setMessage(const QString &text) {
	abortRender();

	message = text;
	clearPages();
	messageLabel->setText(text);
	infoLabel->clear();
}

void PdfPreviewWidget::setInfoText(const QString &text) { infoLabel->setText(text); }

QString PdfPreviewWidget::currentInfoText() const { return infoLabel ? infoLabel->text() : QString(); }

void PdfPreviewWidget::setPdf(const QString &path) {
	pdfPath = path;

	if (isVisible()) {
		render();
	} else {
		pendingRender = true;
		setMessage(tr("Rendering ..."));
	}
}

void PdfPreviewWidget::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);

	if (pendingRender)
		scheduleRender();
}

QString PdfPreviewWidget::findRenderer() const {
	for (const QString &name : {QStringLiteral("pdftocairo"), QStringLiteral("miktex-pdftocairo"),
	                            QStringLiteral("pdftoppm")}) {
		const QString found = QStandardPaths::findExecutable(name);

		if (! found.isEmpty())
			return found;
	}

	return {};
}

void PdfPreviewWidget::fitPagePixmap(QPixmap &pixmap, int cssWidth) {
	const int width = qMax(1, cssWidth);
	pixmap.setDevicePixelRatio(double(pixmap.width()) / double(width));
}

auto PdfPreviewWidget::displayWidthCss() const -> int {
	if (! scroll)
		return minPageWidthCss;

	const int bar = scroll->verticalScrollBar()->sizeHint().width();
	return qBound(minPageWidthCss, scroll->width() - bar - 4, maxPageWidthCss);
}

auto PdfPreviewWidget::renderWidthPx() const -> int {
	const double px = displayWidthCss() * devicePixelRatioF() * supersample;
	return qBound(640, qRound(px), 2600);
}

void PdfPreviewWidget::resizeEvent(QResizeEvent *event) {
	QWidget::resizeEvent(event);

	if (pdfPath.isEmpty() || ! isVisible())
		return;

	const int wanted = displayWidthCss();

	if (renderedWidthCss <= 0 || qAbs(wanted - renderedWidthCss) >= qMax(8, renderedWidthCss / 20))
		scheduleRender();
}

void PdfPreviewWidget::changeEvent(QEvent *event) {
	QWidget::changeEvent(event);

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)

	if (event->type() == QEvent::DevicePixelRatioChange && ! pdfPath.isEmpty() && isVisible())
		scheduleRender();

#endif
}

void PdfPreviewWidget::abortRender() {
	if (! renderProcess)
		return;

	QProcess *process = renderProcess;
	renderProcess = nullptr;
	process->disconnect(this);
	process->kill();
	process->waitForFinished(3000);
	delete process;
}

void PdfPreviewWidget::render() {
	pendingRender = false;

	if (pdfPath.isEmpty()) {
		setMessage(tr("Compile the statement to see the preview here."));
		return;
	}

	if (! QFileInfo::exists(pdfPath)) {
		setMessage(tr("%1 does not exist yet.").arg(QFileInfo(pdfPath).fileName()));
		return;
	}

	const QString renderer = findRenderer();

	if (renderer.isEmpty()) {
		setMessage(tr("pdftocairo / pdftoppm not found in PATH, so the PDF cannot be previewed."));
		return;
	}

	QScrollBar *bar = scroll->verticalScrollBar();
	pendingScrollFraction = bar->maximum() > 0 ? double(bar->value()) / double(bar->maximum()) : 0.0;

	abortRender();
	clearPages();

	QDir().mkpath(tempDir.path());

	for (const QString &old : QDir(tempDir.path()).entryList({QStringLiteral("page-*.png")}, QDir::Files))
		QFile::remove(tempDir.path() + QChar('/') + old);

	messageLabel->setText(tr("Rendering ..."));

	renderedWidthCss = displayWidthCss();
	renderedWidthPx = renderWidthPx();

	const int dpi = qBound(96, qRound(72.0 * renderedWidthPx / a4WidthPt), 400);

	renderProcess = new QProcess(this);
	Lemon::common::suppressConsoleWindow(*renderProcess);
	renderProcess->setWorkingDirectory(tempDir.path());

	connect(renderProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart || ! renderProcess)
			return;

		renderProcess->disconnect(this);
		renderProcess->deleteLater();
		renderProcess = nullptr;
		setMessage(tr("Rendering the PDF preview failed."));
	});
	connect(renderProcess, &QProcess::finished, this, &PdfPreviewWidget::finishRender);

	renderProcess->start(renderer, {QStringLiteral("-png"), QStringLiteral("-r"), QString::number(dpi),
	                                QDir::toNativeSeparators(QFileInfo(pdfPath).absoluteFilePath()),
	                                tempDir.path() + QStringLiteral("/page")});
}

void PdfPreviewWidget::finishRender() {
	QProcess *process = renderProcess;
	renderProcess = nullptr;

	if (! process)
		return;

	const bool ok = process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0;
	process->deleteLater();

	if (! ok) {
		setMessage(tr("Rendering the PDF preview failed."));
		return;
	}

	QStringList files = QDir(tempDir.path()).entryList({QStringLiteral("page-*.png")}, QDir::Files);

	std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
		return pageNumberOf(a) < pageNumberOf(b);
	});

	if (files.isEmpty()) {
		setMessage(tr("Nothing could be rendered from %1.").arg(QFileInfo(pdfPath).fileName()));
		return;
	}

	clearPages();

	for (const QString &fileName : files) {
		QPixmap pixmap(tempDir.path() + QChar('/') + fileName);

		if (pixmap.isNull())
			continue;

		fitPagePixmap(pixmap, renderedWidthCss > 0 ? renderedWidthCss : displayWidthCss());

		auto *label = new QLabel(pagesWidget);
		label->setPixmap(pixmap);
		label->setStyleSheet(QStringLiteral("border: 1px solid #b0b0b0; background: white;"));
		pagesLayout->addWidget(label, 0, Qt::AlignHCenter);
		pageLabels.append(label);
	}

	infoLabel->setText(tr("%1 page(s)").arg(pageLabels.size()));
	messageLabel->clear();

	const double fraction = pendingScrollFraction;
	QTimer::singleShot(0, this, [this, fraction] {
		QScrollBar *scrollBar = scroll->verticalScrollBar();
		scrollBar->setValue(qRound(fraction * scrollBar->maximum()));
	});
}
