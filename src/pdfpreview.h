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

class QProcess;
class QPixmap;
class QScrollArea;
class QTimer;
class QVBoxLayout;

/**
 * statement.pdf 的分页预览。
 *
 * Qt 自带的 Pdf 模块不一定安装，这里用外部渲染器（pdftocairo / pdftoppm，
 * MiKTeX 与常见 TeX 发行版都会带）把每一页转成 PNG 再显示，等价于
 * statement-editor 里用 PyMuPDF 做的预览。
 *
 * 清晰度：页面按**预览窗宽度**显示，渲染时按设备像素比（外加一点超采样）出图，
 * 再把 QPixmap 的 devicePixelRatio 设成「图片宽 ÷ 显示宽」—— 这样 Qt 不会再缩放它，
 * 文字就是屏幕上的实际分辨率（旧版固定 72dpi 渲染后 1:1 贴上去，在 125%/150% 缩放的
 * 屏幕上被放大插值，所以发虚）。面板宽度变了、窗口被拖到另一块屏（像素比变了）都会
 * 防抖重渲染。预览不提供缩放控件：宽度就是预览窗宽度。
 */
class PdfPreviewWidget : public QWidget {
	Q_OBJECT

  public:
	explicit PdfPreviewWidget(QWidget *parent = nullptr);
	~PdfPreviewWidget() override;

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
	/// 页面显示多宽（css 像素）与渲染打算出多少像素宽（测试用）。
	int pageWidthCss() const { return displayWidthCss(); }
	int plannedRenderWidthPx() const { return renderWidthPx(); }
	/// 把渲染出来的页图按 cssWidth 显示：像素比设成 图片宽 ÷ cssWidth。
	/// 这样 Qt 不会再放大它（旧写法不设像素比，缩放屏上会被插值放大 → 糊）。测试也用它。
	static void fitPagePixmap(QPixmap &pixmap, int cssWidth);

  protected:
	/// 选项卡切到题面时才真正渲染（渲染很贵，打开比赛日时它还在后台）。
	void showEvent(QShowEvent *) override;
	/// 预览窗宽度变了就按新宽度重渲染（防抖），否则图会被 Qt 拉伸变糊。
	void resizeEvent(QResizeEvent *) override;
	/// 窗口被拖到另一块屏（缩放比例变了）时像素比变了，也要重渲染。
	void changeEvent(QEvent *) override;

  private:
	void render();
	/// pdftocairo / pdftoppm 渲染结束后的回调（异步，不阻塞界面）。
	void finishRender();
	/// 停掉还没跑完的渲染进程。
	void abortRender();
	/// 延迟一点再渲染（切到题面选项卡时用），避免来回切标签反复跑 pdftocairo。
	void scheduleRender();
	void clearPages();
	QString findRenderer() const;
	/// 一页显示多宽（css 像素）：跟随预览窗宽度，上下限夹一下。
	int displayWidthCss() const;
	/// 想让渲染器输出多少像素宽（设备像素 × 一点超采样）。
	int renderWidthPx() const;

	QString pdfPath;
	QString message;
	QLabel *messageLabel{};
	QScrollArea *scroll{};
	QWidget *pagesWidget{};
	QVBoxLayout *pagesLayout{};
	QLabel *infoLabel{};
	QTimer *renderTimer{};
	/// 异步渲染用的进程（pdftocairo / pdftoppm）。
	QProcess *renderProcess{};
	QList<QLabel *> pageLabels;
	QTemporaryDir tempDir;
	/// 渲染前记下的滚动位置比例，渲染完再恢复。
	double pendingScrollFraction{0.0};
	/// 拿到 PDF 时没显示，就先攒着，等 showEvent 再渲染。
	bool pendingRender{false};
	/// 上一次渲染用的页宽（css 像素）与对应的渲染像素宽，用来判断要不要按新宽度重来。
	int renderedWidthCss{0};
	int renderedWidthPx{0};
};
