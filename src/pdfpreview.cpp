/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pdfpreview.h"
//
#include "base/ProcessUtil.hpp"
//
#include <QComboBox>
#include <QCoreApplication>
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
#include <QWheelEvent>
//
#include <algorithm>

namespace {
	/// `page-3.png` → 3，用于按页码而不是字典序排序。
	int pageNumberOf(const QString &fileName) {
		const QString base = QFileInfo(fileName).completeBaseName();
		const int dash = base.lastIndexOf(QChar('-'));

		if (dash < 0)
			return 0;

		bool ok = false;
		const int number = base.mid(dash + 1).toInt(&ok);
		return ok ? number : 0;
	}
} // namespace

PdfPreviewWidget::PdfPreviewWidget(QWidget *parent) : QWidget(parent) {
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	auto *header = new QHBoxLayout();
	header->addWidget(new QLabel(tr("PDF Preview"), this));
	header->addStretch();
	header->addWidget(new QLabel(tr("Zoom:"), this));
	zoomBox = new QComboBox(this);
	zoomBox->addItems({QStringLiteral("50%"), QStringLiteral("75%"), QStringLiteral("100%"),
	                   QStringLiteral("125%"), QStringLiteral("150%"), QStringLiteral("200%"),
	                   QStringLiteral("300%")});
	zoomBox->setCurrentText(QStringLiteral("100%"));
	zoomBox->setToolTip(tr("Zoom (Ctrl + mouse wheel)"));
	header->addWidget(zoomBox);
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

	connect(zoomBox, &QComboBox::currentTextChanged, this, [this] { scheduleRender(); });
	connect(refreshButton, &QPushButton::clicked, this, [this] { render(); });

	// 连续滚轮缩放时不必每一格都重渲染，攒一小会儿再跑一次。
	renderTimer = new QTimer(this);
	renderTimer->setSingleShot(true);
	renderTimer->setInterval(300);
	connect(renderTimer, &QTimer::timeout, this, [this] { render(); });

	// 预览区里的 Ctrl+滚轮 = 缩放（不按 Ctrl 时仍是普通滚动）。
	scroll->viewport()->installEventFilter(this);
	scroll->installEventFilter(this);

	setMessage(tr("Compile the statement to see the preview here."));
}

void PdfPreviewWidget::scheduleRender() {
	if (renderTimer)
		renderTimer->start();
}

void PdfPreviewWidget::stepZoom(int delta) {
	const int index = zoomBox->currentIndex();
	const int next = qBound(0, index + delta, zoomBox->count() - 1);

	if (next != index)
		zoomBox->setCurrentIndex(next);
}

bool PdfPreviewWidget::eventFilter(QObject *watched, QEvent *event) {
	if (event->type() == QEvent::Wheel) {
		auto *wheel = static_cast<QWheelEvent *>(event);

		if (wheel->modifiers().testFlag(Qt::ControlModifier)) {
			stepZoom(wheel->angleDelta().y() > 0 ? 1 : -1);
			return true;
		}
	}

	return QWidget::eventFilter(watched, event);
}

void PdfPreviewWidget::clearPages() {
	for (QLabel *label : pageLabels) {
		pagesLayout->removeWidget(label);
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
	message = text;
	clearPages();
	messageLabel->setText(text);
	infoLabel->clear();
}

void PdfPreviewWidget::setPdf(const QString &path) {
	pdfPath = path;

	// 渲染要起 pdftocairo 把每页转成 PNG 再读成 QPixmap，大题干能卡好几秒。
	// 选项卡没显示就先记下来，等切过来（showEvent）再渲染。
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

int PdfPreviewWidget::renderDpi() const {
	const int percent = zoomBox->currentText().remove(QChar('%')).toInt();
	return qMax(48, 96 * percent / 100);
}

void PdfPreviewWidget::render() {
	pendingRender = false;

	if (pdfPath.isEmpty()) {
		setMessage(tr("Compile the statement to see the preview here."));
		return;
	}

	if (! QFileInfo::exists(pdfPath)) {
		setMessage(tr("statement.pdf does not exist yet."));
		return;
	}

	// 重新渲染后尽量把视图留在原来的位置（要在清空页面之前记下比例）
	QScrollBar *bar = scroll->verticalScrollBar();
	const double fraction = bar->maximum() > 0 ? double(bar->value()) / double(bar->maximum()) : 0.0;

	clearPages();

	const QString renderer = findRenderer();

	if (renderer.isEmpty()) {
		setMessage(tr("pdftocairo / pdftoppm not found in PATH, so the PDF cannot be previewed."));
		return;
	}

	QDir().mkpath(tempDir.path());

	// 清掉上一轮的图片，避免残留旧页
	for (const QString &old : QDir(tempDir.path()).entryList({QStringLiteral("page-*.png")}, QDir::Files))
		QFile::remove(tempDir.path() + QChar('/') + old);

	messageLabel->setText(tr("Rendering ..."));
	QCoreApplication::processEvents();

	QProcess process;
	Lemon::common::suppressConsoleWindow(process);
	process.setWorkingDirectory(tempDir.path());
	process.start(renderer, {QStringLiteral("-png"), QStringLiteral("-r"), QString::number(renderDpi()),
	                         QDir::toNativeSeparators(QFileInfo(pdfPath).absoluteFilePath()),
	                         tempDir.path() + QStringLiteral("/page")});

	if (! process.waitForStarted(15000) || ! process.waitForFinished(-1)) {
		process.kill();
		setMessage(tr("Rendering the PDF preview failed."));
		return;
	}

	QStringList files = QDir(tempDir.path()).entryList({QStringLiteral("page-*.png")}, QDir::Files);

	std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
		return pageNumberOf(a) < pageNumberOf(b);
	});

	if (files.isEmpty()) {
		setMessage(tr("Nothing could be rendered from statement.pdf."));
		return;
	}

	clearPages();

	for (const QString &fileName : files) {
		QPixmap pixmap(tempDir.path() + QChar('/') + fileName);

		if (pixmap.isNull())
			continue;

		auto *label = new QLabel(pagesWidget);
		label->setPixmap(pixmap);
		label->setStyleSheet(QStringLiteral("border: 1px solid #b0b0b0; background: white;"));
		pagesLayout->addWidget(label, 0, Qt::AlignHCenter);
		pageLabels.append(label);
	}

	infoLabel->setText(tr("%1 page(s) · zoom %2").arg(pageLabels.size()).arg(zoomBox->currentText()));
	messageLabel->clear();

	QTimer::singleShot(0, this, [this, fraction] {
		QScrollBar *scrollBar = scroll->verticalScrollBar();
		scrollBar->setValue(qRound(fraction * scrollBar->maximum()));
	});
}
