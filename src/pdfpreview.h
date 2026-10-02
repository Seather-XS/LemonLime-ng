/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include <QLabel>
#include <QList>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>

class QComboBox;
class QScrollArea;
class QTimer;
class QVBoxLayout;

/**
 * statement.pdf 的分页预览。
 *
 * Qt 自带的 Pdf 模块不一定安装，这里用外部渲染器（pdftocairo / pdftoppm，
 * MiKTeX 与常见 TeX 发行版都会带）把每一页转成 PNG 再显示，等价于
 * statement-editor 里用 PyMuPDF 做的预览。
 */
class PdfPreviewWidget : public QWidget {
	Q_OBJECT

  public:
	explicit PdfPreviewWidget(QWidget *parent = nullptr);

	/// 显示一句占位文字（还没编译、正在编译、渲染器缺失……）。
	void setMessage(const QString &message);
	/// 渲染并显示指定的 PDF。
	void setPdf(const QString &path);
	/// 底部信息行（渲染出页面时是「N 页 · 缩放 …」，没有页面时用来写「期望的文件路径」）。
	void setInfoText(const QString &text);
	/// 清空预览。
	void clear();

	/// 当前预览指向的文件（测试用）。
	QString currentPdf() const { return pdfPath; }
	/// 当前已渲染出来的页数（测试用）。
	int renderedPages() const { return pageLabels.size(); }
	/// 底部信息行的内容（测试用）。
	QString currentInfoText() const;

  protected:
	/// 在预览区里 Ctrl+滚轮 = 缩放。
	bool eventFilter(QObject *watched, QEvent *event) override;
	/// 选项卡切到题面时才真正渲染（渲染很贵，打开比赛日时它还在后台）。
	void showEvent(QShowEvent *) override;

  private:
	void render();
	/// 连续滚轮时延迟一点再重渲染，避免每一格都跑一次 pdftocairo。
	void scheduleRender();
	/// 按缩放档位步进（delta 为 +1 / -1）。
	void stepZoom(int delta);
	void clearPages();
	QString findRenderer() const;
	int renderDpi() const;

	QString pdfPath;
	QString message;
	QLabel *messageLabel{};
	QScrollArea *scroll{};
	QWidget *pagesWidget{};
	QVBoxLayout *pagesLayout{};
	QComboBox *zoomBox{};
	QLabel *infoLabel{};
	QTimer *renderTimer{};
	QList<QLabel *> pageLabels;
	QTemporaryDir tempDir;
	/// 拿到 PDF 时没显示，就先攒着，等 showEvent 再渲染。
	bool pendingRender{false};
};
