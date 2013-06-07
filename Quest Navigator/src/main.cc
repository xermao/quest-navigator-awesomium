#include "common/application.h"
#include "common/view.h"
#include "common/method_dispatcher.h"
#include "deps/qsp/qsp.h"
#include "deps/qsp/bindings/default/qsp_default.h"
#include <Awesomium/WebCore.h>
#include <Awesomium/DataPak.h>
#include <Awesomium/STLHelpers.h>
#ifdef _WIN32
#include <Windows.h>
#endif

using namespace Awesomium;

class WebUISample : public Application::Listener {
  Application* app_;
  View* view_;
  DataSource* data_source_;
  MethodDispatcher method_dispatcher_;
 public:
  WebUISample() 
    : app_(Application::Create()),
      view_(0),
      data_source_(0) {
    app_->set_listener(this);
  }

  virtual ~WebUISample() {
    if (view_)
      app_->DestroyView(view_);
    if (data_source_)
      delete data_source_;
    if (app_)
      delete app_;
  }

  void Run() {
    app_->Run();
  }

  // Inherited from Application::Listener
  virtual void OnLoaded() {
    view_ = View::Create(512, 512);

    BindMethods(view_->web_view());

	QSPInit();

    data_source_ = new DataPakSource(ToWebString("webui_assets.pak"));
    view_->web_view()->session()->AddDataSource(WSLit("webui"), data_source_);

    // Load the page asynchronously from the resource PAK.
    view_->web_view()->LoadURL(WebURL(WSLit("asset://webui/page.html")));
  }

  // Inherited from Application::Listener
  virtual void OnUpdate() {
  }

  // Inherited from Application::Listener
  virtual void OnShutdown() {
  }

  void BindMethods(WebView* web_view) {
    // Create a Global JS Object named 'App'
    JSValue result = web_view->CreateGlobalJavascriptObject(WSLit("App"));
    if (result.IsObject()) {
      // Bind our custom methods to it.
      JSObject& app_object = result.ToObject();
      method_dispatcher_.Bind(app_object,
        WSLit("SayHello"),
        JSDelegate(this, &WebUISample::OnSayHello));
      method_dispatcher_.Bind(app_object,
        WSLit("Exit"),
        JSDelegate(this, &WebUISample::OnExit));
      method_dispatcher_.BindWithRetval(app_object,
        WSLit("GetSecretMessage"),
        JSDelegateWithRetval(this, &WebUISample::OnGetSecretMessage));
    }

    // Bind our method dispatcher to the WebView
    web_view->set_js_method_handler(&method_dispatcher_);
  }

  // Bound to App.SayHello() in JavaScript
  void OnSayHello(WebView* caller,
                  const JSArray& args) {
    app_->ShowMessage("Hello!");
  }

  // Bound to App.Exit() in JavaScript
  void OnExit(WebView* caller,
              const JSArray& args) {
    app_->Quit();
  }

  // Bound to App.GetSecretMessage() in JavaScript
  JSValue OnGetSecretMessage(WebView* caller,
                             const JSArray& args) {
    return JSValue(WSLit(
    "<img src='asset://webui/key.png'/> You have unlocked the secret message!"));
  }
};

#ifdef _WIN32
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, wchar_t*, 
  int nCmdShow) {
#else
int main() {
#endif

  WebUISample sample;
  sample.Run();

  return 0;
}
