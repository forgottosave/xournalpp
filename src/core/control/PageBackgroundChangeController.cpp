#include "PageBackgroundChangeController.h"

#include <memory>   // for __shared_ptr...
#include <string>   // for allocator
#include <utility>  // for move

#include <gdk-pixbuf/gdk-pixbuf.h>  // for gdk_pixbuf_g...
#include <gio/gio.h>                // for GFile
#include <glib.h>                   // for g_error_free

#include "control/settings/PageTemplateSettings.h"       // for PageTemplate...
#include "control/settings/Settings.h"                   // for Settings
#include "control/stockdlg/ImageOpenDlg.h"               // for ImageOpenDlg
#include "gui/MainWindow.h"                              // for MainWindow
#include "gui/dialog/backgroundSelect/ImagesDialog.h"    // for ImagesDialog
#include "gui/dialog/backgroundSelect/PdfPagesDialog.h"  // for PdfPagesDialog
#include "gui/menus/menubar/Menubar.h"                   // for Menubar
#include "gui/menus/menubar/PageTypeSubmenu.h"           // for PageTypeSubmenu
#include "model/BackgroundImage.h"                       // for BackgroundImage
#include "model/Document.h"                              // for Document
#include "model/PageType.h"                              // for PageType
#include "model/XojPage.h"                               // for XojPage
#include "pdf/base/XojPdfPage.h"                         // for XojPdfPageSPtr
#include "undo/GroupUndoAction.h"                        // for GroupUndoAction
#include "undo/PageBackgroundChangedUndoAction.h"        // for PageBackgrou...
#include "undo/UndoAction.h"                             // for UndoAction
#include "undo/UndoRedoHandler.h"                        // for UndoRedoHandler
#include "util/Assert.h"                                 // for xoj_assert
#include "util/PathUtil.h"                               // for fromGFile
#include "util/Util.h"                                   // for npos
#include "util/XojMsgBox.h"                              // for XojMsgBox
#include "util/i18n.h"                                   // for FS, _, _F

#include "Control.h"  // for Control


PageBackgroundChangeController::PageBackgroundChangeController(Control* control): control(control) {
    registerListener(control);
}

void PageBackgroundChangeController::applyBackgroundToAllPages(const PageType& pt) {
    control->clearSelectionEndText();

    Document* doc = control->getDocument();

    auto groupUndoAction = std::make_unique<GroupUndoAction>();

    for (size_t p = 0; p < doc->getPageCount(); p++) {
        auto undoAction = commitPageTypeChange(p, pt);
        if (undoAction) {
            groupUndoAction->addAction(std::move(undoAction));
        }
    }

    control->getUndoRedoHandler()->addUndoAction(std::move(groupUndoAction));

    control->getWindow()->getMenubar()->getPageTypeSubmenu().setSelected(pt);
}

void PageBackgroundChangeController::applyCurrentPageBackgroundToAll() {
    PageType pt = control->getCurrentPage()->getBackgroundType();
    applyBackgroundToAllPages(pt);
}

void PageBackgroundChangeController::changeCurrentPageBackground(const PageType& pageType) {
    control->clearSelectionEndText();

    PageRef page = control->getCurrentPage();
    if (!page) {
        return;
    }

    Document* doc = control->getDocument();
    const size_t pageNr = doc->indexOf(page);
    xoj_assert(pageNr != npos);

    auto undoAction = commitPageTypeChange(pageNr, pageType);
    if (undoAction) {
        control->getUndoRedoHandler()->addUndoAction(std::move(undoAction));
    }

    control->getWindow()->getMenubar()->getPageTypeSubmenu().setSelected(pageType);
}

void PageBackgroundChangeController::setPageTypeForNewPages(const std::optional<PageType>& pt) {
    this->pageTypeForNewPages = pt;
}

auto PageBackgroundChangeController::commitPageTypeChange(const size_t pageNum, const PageType& pageType)
        -> std::unique_ptr<UndoAction> {
    PageRef page = control->getDocument()->getPage(pageNum);
    if (!page) {
        return {};
    }

    Document* doc = control->getDocument();
    const size_t pageNr = doc->indexOf(page);
    xoj_assert(pageNr != npos);

    // Get values for Undo / Redo
    const double origW = page->getWidth();
    const double origH = page->getHeight();
    BackgroundImage origBackgroundImage = page->getBackgroundImage();
    const size_t origPdfPage = page->getPdfPageNr();
    PageType origType = page->getBackgroundType();

    // Apply the new background
    applyPageBackground(page, pageType);

    control->firePageChanged(pageNr);
    control->updateBackgroundSizeButton();
    return std::make_unique<PageBackgroundChangedUndoAction>(page, origType, origPdfPage, origBackgroundImage, origW,
                                                             origH);
}

/**
 * Apply a new Image Background, asks the user which image should be inserted
 *
 * @return true on success, false if the user cancels
 */
auto PageBackgroundChangeController::applyImageBackground(PageRef page) -> bool {
    Document* doc = control->getDocument();

    doc->lock();
    ImagesDialog dlg(control->getGladeSearchPath(), doc, control->getSettings());
    doc->unlock();

    dlg.show(GTK_WINDOW(control->getGtkWindow()));
    BackgroundImage img = dlg.getSelectedImage();

    if (!img.isEmpty()) {
        page->setBackgroundImage(img);
        page->setBackgroundType(PageType(PageTypeFormat::Image));
    } else if (dlg.shouldShowFilechooser()) {
        bool attach = false;
        GFile* file = ImageOpenDlg::show(control->getGtkWindow(), control->getSettings(), true, &attach);
        if (file == nullptr) {
            // The user canceled
            return false;
        }


        auto filepath = Util::fromGFile(file);

        BackgroundImage newImg;
        GError* err = nullptr;
        newImg.loadFile(filepath, &err);
        newImg.setAttach(attach);
        if (err) {
            XojMsgBox::showErrorToUser(control->getGtkWindow(),
                                       FS(_F("This image could not be loaded. Error message: {1}") % err->message));
            g_error_free(err);
            return false;
        }


        page->setBackgroundImage(newImg);
        page->setBackgroundType(PageType(PageTypeFormat::Image));
    }

    // Apply correct page size
    GdkPixbuf* pixbuf = page->getBackgroundImage().getPixbuf();
    if (pixbuf) {
        page->setSize(gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));

        size_t pageNr = doc->indexOf(page);
        if (pageNr != npos) {
            // Only if the page is already inserted into the document
            control->firePageSizeChanged(pageNr);
        }
    }

    return true;
}

/**
 * Apply a new PDF Background, asks the user which page should be selected
 *
 * @return true on success, false if the user cancels
 */
auto PageBackgroundChangeController::applyPdfBackground(PageRef page) -> bool {
    Document* doc = control->getDocument();

    if (doc->getPdfPageCount() == 0) {

        std::string msg = _("You don't have any PDF pages to select from. Cancel operation.\n"
                            "Please select another background type: Menu \"Journal\" → \"Configure Page Template\".");
        XojMsgBox::showErrorToUser(control->getGtkWindow(), msg);
        return false;
    }

    doc->lock();
    auto dlg = PdfPagesDialog(control->getGladeSearchPath(), doc, control->getSettings());
    doc->unlock();

    dlg.show(GTK_WINDOW(control->getGtkWindow()));

    size_t selected = dlg.getSelectedPage();

    if (selected < doc->getPdfPageCount()) {
        // no need to set a type, if we set the page number the type is also set
        page->setBackgroundPdfPageNr(selected);

        XojPdfPageSPtr p = doc->getPdfPage(selected);
        page->setSize(p->getWidth(), p->getHeight());
    }

    return true;
}

/**
 * Apply the background to the page, asks for PDF Page or Image, if needed
 *
 * @return true on success, false if the user cancels
 */
auto PageBackgroundChangeController::applyPageBackground(PageRef page, const PageType& pt) -> bool {
    if (pt.isPdfPage()) {
        return applyPdfBackground(page);
    }
    if (pt.isImagePage()) {
        return applyImageBackground(page);
    }


    page->setBackgroundType(pt);
    return true;
}

/**
 * Copy the background from source to target
 */
void PageBackgroundChangeController::copyBackgroundFromOtherPage(PageRef target, PageRef source) {
    // Copy page size
    target->setSize(source->getWidth(), source->getHeight());

    // Copy page background type
    PageType bg = source->getBackgroundType();
    target->setBackgroundType(bg);

    if (bg.isPdfPage()) {
        // If PDF: Copy PDF Page
        target->setBackgroundPdfPageNr(source->getPdfPageNr());
    } else if (bg.isImagePage()) {
        // If Image: Copy the Image
        target->setBackgroundImage(source->getBackgroundImage());
    } else {
        // Copy the background color
        target->setBackgroundColor(source->getBackgroundColor());
    }
}

void PageBackgroundChangeController::insertNewPage(size_t position, bool shouldScrollToPage) {
    control->clearSelectionEndText();

    Document* doc = control->getDocument();
    if (position > doc->getPageCount()) {
        position = doc->getPageCount();
    }

    PageTemplateSettings model;
    model.parse(control->getSettings()->getPageTemplate());

    auto page = std::make_shared<XojPage>(model.getPageWidth(), model.getPageHeight());
    PageRef current = control->getCurrentPage();

    if (!pageTypeForNewPages) {
        xoj_assert(current);
        copyBackgroundFromOtherPage(page, current);
    } else {
        // Create a new page from template
        if (!applyPageBackground(page, pageTypeForNewPages.value())) {
            // User canceled PDF or Image Selection
            return;
        }

        // Set background Color
        page->setBackgroundColor(model.getBackgroundColor());

        if (model.isCopyLastPageSize()) {
            xoj_assert(current);
            page->setSize(current->getWidth(), current->getHeight());
        }
    }

    control->insertPage(page, position, shouldScrollToPage);
}

void PageBackgroundChangeController::documentChanged(DocumentChangeType type) {}

void PageBackgroundChangeController::pageSizeChanged(size_t page) {}

void PageBackgroundChangeController::pageChanged(size_t page) {}

void PageBackgroundChangeController::pageInserted(size_t page) {}

void PageBackgroundChangeController::pageDeleted(size_t page) {}

void PageBackgroundChangeController::pageSelected(size_t page) {
    auto const& current = control->getCurrentPage();
    if (!current) {
        return;
    }

    control->getWindow()->getMenubar()->getPageTypeSubmenu().setSelected(current->getBackgroundType());
}
