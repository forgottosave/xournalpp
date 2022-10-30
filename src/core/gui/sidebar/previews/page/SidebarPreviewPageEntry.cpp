#include "SidebarPreviewPageEntry.h"

#include "control/Control.h"                                // for Control
#include "control/ScrollHandler.h"                          // for ScrollHan...
#include "gui/sidebar/previews/page/SidebarPreviewPages.h"  // for SidebarPr...

SidebarPreviewPageEntry::SidebarPreviewPageEntry(SidebarPreviewPages* sidebar, const PageRef& page):
        SidebarPreviewBaseEntry(sidebar, page), sidebar(sidebar) {
}

SidebarPreviewPageEntry::~SidebarPreviewPageEntry() = default;

auto SidebarPreviewPageEntry::getRenderType() -> PreviewRenderType { return RENDER_TYPE_PAGE_PREVIEW; }

void SidebarPreviewPageEntry::mouseButtonClickCallback() {
    std::cout << "mouseButtonPressCallback() to page " << page << std::endl;
    sidebar->getControl()->getScrollHandler()->scrollToPage(page);
    sidebar->getControl()->firePageSelected(page);
}

void SidebarPreviewPageEntry::mouseButtonPressCallback() {
    std::cout << "Key pressed, also clicked !!!" << std::endl;

    sidebar->getControl()->getScrollHandler()->scrollToPage(page);
    sidebar->getControl()->firePageSelected(page);

    GdkDisplay *display = gdk_display_get_default ();
    GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
    GdkDevice *device = gdk_device_manager_get_client_pointer (device_manager);
    int x;
    int y;
    gdk_device_get_position (device, NULL, &x, &y);
    std::cout << "    -> position of device: " << x << ", " << y << std::endl;
    this->x = x;
    this->y = y;
}

void SidebarPreviewPageEntry::mouseButtonReleaseCallback() {
    std::cout << "Key released ---" << std::endl;
    GdkDisplay *display = gdk_display_get_default ();
    GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
    GdkDevice *device = gdk_device_manager_get_client_pointer (device_manager);
    int x;
    int y;
    gdk_device_get_position (device, NULL, &x, &y);
    std::cout << "    -> position of device: " << x << ", " << y << std::endl;

    int pagesMoved = (x - this->x) / this->getWidgetWidth() + 2 * (y - this->y) / this->getWidgetHeight(); // TODO 2 = #columns
    std::cout << "    -> pages moved: " << pagesMoved << std::endl;
    if (pagesMoved != 0) {
        for (int i=pagesMoved; i<0; i++) {
            sidebar->actionPerformed(SIDEBAR_ACTION_MOVE_UP);
        }
        for (int i=pagesMoved; i>0; i--) {
            sidebar->actionPerformed(SIDEBAR_ACTION_MOVE_DOWN);
        }
    }
}
