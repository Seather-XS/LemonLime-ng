/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pdfpreview.h"
//
#include "base/ProcessUtil.hpp"
//
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
//
#include <algorithm>

namespace {
	/// 预览缩放固定为 75%（与原来的「75%」档位效果一致）。界面上不再提供缩放控件。
	constexpr int previewZoomPercent = 75;

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

	// 切到题面选项卡时不立刻渲染，稍等一下，避免来回切标签时反复跑 pdftocairo。
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
		// deleteLater() 要等回到事件循环才真的删掉，其间旧图还贴在界面上（
		// 渲染一次要好几秒，用户就会看到旧页面）：先藏起来，保证立刻消失。
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
	// 换提示文字通常意味着要显示别的东西了（比如「正在编译」），
	// 顺手停掉还在跑的渲染，免得它稍后把提示顶掉。
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
	// 缩放固定：96 * 75 / 100 = 72。
	return qMax(48, 96 * previewZoomPercent / 100);
}

// 停掉还没跑完的渲染：先断开信号，否则 kill 后还会触发 finishRender 把新一波的结果顶掉。
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

	// 重新渲染后尽量把视图留在原来的位置（要在清空页面之前记下比例）
	QScrollBar *bar = scroll->verticalScrollBar();
	pendingScrollFraction = bar->maximum() > 0 ? double(bar->value()) / double(bar->maximum()) : 0.0;

	// 上一次还没跑完就先停掉，避免两次输出乱在一起
	abortRender();
	clearPages();

	QDir().mkpath(tempDir.path());

	// 清掉上一轮的图片，避免残留旧页
	for (const QString &old : QDir(tempDir.path()).entryList({QStringLiteral("page-*.png")}, QDir::Files))
		QFile::remove(tempDir.path() + QChar('/') + old);

	messageLabel->setText(tr("Rendering ..."));

	// pdftocairo 渲染大题干要好几秒。这里用异步信号等它结束，
	// 不能再用 waitForFinished()——那会像以前一样把整个界面卡死。
	renderProcess = new QProcess(this);
	Lemon::common::suppressConsoleWindow(*renderProcess);
	renderProcess->setWorkingDirectory(tempDir.path());

	connect(renderProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		// 起不来（比如路径失效）时不会发 finished，只能在这里收拾；
		// 其它错误（崩溃）会接着发 finished，交给 finishRender() 统一处理。
		if (error != QProcess::FailedToStart || ! renderProcess)
			return;

		renderProcess->disconnect(this);
		renderProcess->deleteLater();
		renderProcess = nullptr;
		setMessage(tr("Rendering the PDF preview failed."));
	});
	connect(renderProcess, &QProcess::finished, this, &PdfPreviewWidget::finishRender);

	renderProcess->start(renderer, {QStringLiteral("-png"), QStringLiteral("-r"), QString::number(renderDpi()),
	                                QDir::toNativeSeparators(QFileInfo(pdfPath).absoluteFilePath()),
	                                tempDir.path() + QStringLiteral("/page")});
}

void PdfPreviewWidget::finishRender() {
	QProcess *process = renderProcess;
	renderProcess = nullptr;

	if (! process)
		return; // 已经被 abortRender() 接管

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
