/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QLabel>
#include <QList>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>

class QProcess;
class QPixmap;
class QScrollArea;
class QTimer;
class QVBoxLayout;

class PdfPreviewWidget : public QWidget {
	Q_OBJECT

  public:
	explicit PdfPreviewWidget(QWidget *parent = nullptr);
	~PdfPreviewWidget() override;

	void setMessage(const QString &message);
	void setPdf(const QString &path);
	void setInfoText(const QString &text);
	void clear();

	QString currentPdf() const { return pdfPath; }
	int renderedPages() const { return pageLabels.size(); }
	QString currentInfoText() const;
	int pageWidthCss() const { return displayWidthCss(); }
	int plannedRenderWidthPx() const { return renderWidthPx(); }
	static void fitPagePixmap(QPixmap &pixmap, int cssWidth);

  protected:
	void showEvent(QShowEvent *) override;
	void resizeEvent(QResizeEvent *) override;
	void changeEvent(QEvent *) override;

  private:
	void render();
	void finishRender();
	void abortRender();
	void scheduleRender();
	void clearPages();
	QString findRenderer() const;
	int displayWidthCss() const;
	int renderWidthPx() const;

	QString pdfPath;
	QString message;
	QLabel *messageLabel{};
	QScrollArea *scroll{};
	QWidget *pagesWidget{};
	QVBoxLayout *pagesLayout{};
	QLabel *infoLabel{};
	QTimer *renderTimer{};
	QProcess *renderProcess{};
	QList<QLabel *> pageLabels;
	QTemporaryDir tempDir;
	double pendingScrollFraction{0.0};
	bool pendingRender{false};
	int renderedWidthCss{0};
	int renderedWidthPx{0};
};
