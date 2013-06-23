#include "listener.h"

#include "../common/application.h"
#include "../common/view.h"
#include "../common/method_dispatcher.h"
#include "../deps/qsp/qsp.h"
#include "../deps/qsp/bindings/default/qsp_default.h"
#include <Awesomium/WebCore.h>
#include <Awesomium/DataPak.h>
#include <Awesomium/STLHelpers.h>
#include "utils.h"
#include "configuration.h"
#include "skin.h"
#include <process.h>
#include <algorithm>

using namespace Awesomium;

namespace QuestNavigator {

	// Глобальные переменные для работы с потоками

	// Структура для критических секций
	CRITICAL_SECTION g_csSharedData;

	// Разделяемые данные
	struct
	{
		string str;
		int num;
		JSValue jsValue;
	} g_sharedData;

	// События для синхронизации потоков
	HANDLE g_eventList[evLast];

	QnApplicationListener::QnApplicationListener() 
		: app_(Application::Create()),
		view_(0),
		data_source_(0),
		resource_interceptor_(),

		gameIsRunning(false),

		libThread(NULL)
	{
		app_->set_listener(this);
	}

	QnApplicationListener::~QnApplicationListener() {
		if (view_)
			app_->DestroyView(view_);
		if (data_source_)
			delete data_source_;
		if (app_)
			delete app_;
	}

	void QnApplicationListener::Run() {
		app_->Run();
	}

	// Inherited from Application::Listener
	void QnApplicationListener::OnLoaded() {
		view_ = View::Create(512, 512);

		// Перехватчик запросов, выполняющихся при нажатии на ссылку.
		resource_interceptor_.setApp(app_);
		app_->web_core()->set_resource_interceptor(&resource_interceptor_);

		BindMethods(view_->web_view());

		initLib();

		// Подключаем пак с данными по умолчанию.
		// Существование пака не проверяется.
		// Если нам в параметрах указали путь к файлу - тогда просто не используем пак.
		data_source_ = new DataPakSource(ToWebString("assets.pak"));
		view_->web_view()->session()->AddDataSource(WSLit("webui"), data_source_);

		QuestNavigator::initOptions();

		std::string url = QuestNavigator::getContentUrl();
		view_->web_view()->LoadURL(WebURL(ToWebString(url)));
	}

	// Inherited from Application::Listener
	void QnApplicationListener::OnUpdate() {
		if (checkForSingle(evJsCommitted)) {
			// Библиотека сообщила потоку Ui,
			// что требуется выполнить JS-запрос.
			processLibJsCall();
		};
	}

	// Inherited from Application::Listener
	void QnApplicationListener::OnShutdown() {
		FreeResources();
	}

	void QnApplicationListener::BindMethods(WebView* web_view) {
		// Создаём глобальный JS-объект "QspLib"
		JSValue result = web_view->CreateGlobalJavascriptObject(WSLit("QspLibAwesomium"));
		if (result.IsObject()) {
			JSObject& app_object = result.ToObject();

			// Привязываем колбэки
			method_dispatcher_.Bind(app_object,
				WSLit("restartGame"),
				JSDelegate(this, &QnApplicationListener::restartGame));

			method_dispatcher_.Bind(app_object,
				WSLit("executeAction"),
				JSDelegate(this, &QnApplicationListener::executeAction));

			method_dispatcher_.Bind(app_object,
				WSLit("selectObject"),
				JSDelegate(this, &QnApplicationListener::selectObject));

			method_dispatcher_.Bind(app_object,
				WSLit("loadGame"),
				JSDelegate(this, &QnApplicationListener::loadGame));

			method_dispatcher_.Bind(app_object,
				WSLit("saveGame"),
				JSDelegate(this, &QnApplicationListener::saveGame));

			method_dispatcher_.Bind(app_object,
				WSLit("saveSlotSelected"),
				JSDelegate(this, &QnApplicationListener::saveSlotSelected));

			method_dispatcher_.Bind(app_object,
				WSLit("msgResult"),
				JSDelegate(this, &QnApplicationListener::msgResult));

			method_dispatcher_.Bind(app_object,
				WSLit("errorResult"),
				JSDelegate(this, &QnApplicationListener::errorResult));

			method_dispatcher_.Bind(app_object,
				WSLit("userMenuResult"),
				JSDelegate(this, &QnApplicationListener::userMenuResult));

			method_dispatcher_.Bind(app_object,
				WSLit("inputResult"),
				JSDelegate(this, &QnApplicationListener::inputResult));

			method_dispatcher_.Bind(app_object,
				WSLit("setMute"),
				JSDelegate(this, &QnApplicationListener::setMute));

			// Привязываем колбэк для обработки вызовов alert
			method_dispatcher_.Bind(app_object,
				WSLit("alert"),
				JSDelegate(this, &QnApplicationListener::alert));
		}

		// Привязываем обработчик колбэков к WebView
		web_view->set_js_method_handler(&method_dispatcher_);
	}

	// ********************************************************************
	// ********************************************************************
	// ********************************************************************
	//                   Основная логика
	// ********************************************************************
	// ********************************************************************
	// ********************************************************************

	void QnApplicationListener::initLib()
	{
		gameIsRunning = false;

		//      //Создаем список для всплывающего меню
		//      menuList = new Vector<ContainerMenuItem>();

		//      //Создаем объект для таймера
		//      timerHandler = new Handler(Looper.getMainLooper());

		//Запускаем поток библиотеки
		StartLibThread();
	}

	void QnApplicationListener::FreeResources()
	{
		//Контекст UI

		//Процедура "честного" высвобождения всех ресурсов - в т.ч. остановка потока библиотеки

		//Очищаем ВСЕ на выходе
		if (gameIsRunning)
		{
			StopGame(false);
		}
		//Останавливаем поток библиотеки
		StopLibThread();
	}

	void QnApplicationListener::runGame(string fileName)
	{
		// Контекст UI

		// Готовим данные для передачи в поток
		lockData();
		g_sharedData.str = fileName;
		runSyncEvent(evRunGame);
		unlockData();

		gameIsRunning = true;

		////Контекст UI
		//if (!Utility.CheckAssetExists(uiContext, fileName, "runGame"))
		//	return;

		//if (libThreadHandler == null)
		//{
		//	Utility.WriteLog("runGame: failed, libThreadHandler is null");
		//	return;
		//}

		//if (libraryThreadIsRunning)
		//{
		//	Utility.WriteLog("runGame: failed, library thread is already running");
		//	return;
		//}

		//libThreadHandler.post(new Runnable() {
		//	public void run() {
		//		InputStream fIn = null;
		//		int size = 0;

		//		AssetManager assetManager = uiContext.getAssets();
		//		if (assetManager == null)
		//		{
		//			Utility.WriteLog("runGame(in library thread): failed, assetManager is null");
		//			return;
		//		}
		//		try {
		//			fIn = assetManager.open(gameFileName);
		//			size = fIn.available();
		//		} catch (Exception e) {
		//			e.printStackTrace();
		//			Utility.ShowError(uiContext, "Не удалось получить доступ к файлу");
		//			try {
		//				fIn.close();
		//			} catch (IOException e1) {
		//				Utility.ShowError(uiContext, "Не удалось освободить дескриптор файла");
		//				e1.printStackTrace();
		//			}
		//			return;
		//		}

		//		byte[] inputBuffer = new byte[size];
		//		try {
		//			// Fill the Buffer with data from the file
		//			fIn.read(inputBuffer);
		//			fIn.close();
		//		} catch (IOException e) {
		//			e.printStackTrace();
		//			Utility.ShowError(uiContext, "Не удалось прочесть файл");
		//			return;
		//		}

		//		if (!inited)
		//			QSPInit();
		//		final bool gameLoaded = QSPLoadGameWorldFromData(inputBuffer, size, gameFileName);
		//		CheckQspResult(gameLoaded, "runGame: QSPLoadGameWorldFromData");

		//		if (gameLoaded)
		//		{
		//			mainActivity.runOnUiThread(new Runnable() {
		//				public void run() {
		//					//Запускаем таймер
		//					timerInterval = 500;
		//					timerStartTime = System.currentTimeMillis();
		//					timerHandler.removeCallbacks(timerUpdateTask);
		//					timerHandler.postDelayed(timerUpdateTask, timerInterval);

		//					//Запускаем счетчик миллисекунд
		//					gameStartTime = System.currentTimeMillis();

		//					//Все готово, запускаем игру
		//					libThreadHandler.post(new Runnable() {
		//						public void run() {
		//							libraryThreadIsRunning = true;
		//							bool result = QSPRestartGame(true);
		//							CheckQspResult(result, "runGame: QSPRestartGame");
		//							libraryThreadIsRunning = false;
		//						}
		//					});

		//					gameIsRunning = true;
		//				}
		//			});
		//		}
		//	}
		//});
	}

	void QnApplicationListener::StopGame(bool restart)
	{
		//Контекст UI
		if (gameIsRunning)
		{
			runSyncEvent(evStopGame);
			// Возможен Deadlock при закрытии окна, 
			// когда библиотека ждёт ответа интерфейса.
			// Когда-нибудь нужно с этим разобраться.
			waitForSingle(evGameStopped);
			gameIsRunning = false;
		}
	}

	// Выполнение строки кода
	void QnApplicationListener::executeCode(string qspCode)
	{
		//Контекст UI
		if (gameIsRunning)
		{
			lockData();
			g_sharedData.str = qspCode;
			runSyncEvent(evExecuteCode);
			unlockData();
		}
	}

	// Курсор находится в текстовом поле
	bool QnApplicationListener::textInputIsFocused()
	{
		return view_->web_view()->focused_element_type() == kFocusedElementType_TextInput;
	}

	// ********************************************************************
	// ********************************************************************
	// ********************************************************************
	//                       Колбэки интерпретатора
	// ********************************************************************
	// ********************************************************************
	// ********************************************************************

	void QnApplicationListener::RefreshInt(int isRedraw) 
	{
		//showMessage("RefreshInt called!", "");
		//Контекст библиотеки
		bool needUpdate = Skin::isSomethingChanged();
		Skin::updateBaseVars();
		needUpdate = needUpdate || Skin::isSomethingChanged();
		Skin::updateMainScreen();
		needUpdate = needUpdate || Skin::isSomethingChanged();

		JSObject jsSkin;
		bool bSkinPrepared = false;
		if (needUpdate) {
			jsSkin = Skin::getJsSkin();
			bSkinPrepared = true;
		}

		//основное описание
		string mainDesc = "";
		bool bMainDescPrepared = false;
		if ((QSPIsMainDescChanged() == QSP_TRUE) || Skin::isHtmlModeChanged)
		{
			mainDesc = Skin::applyHtmlFixes(fromQsp(QSPGetMainDesc()));
			bMainDescPrepared = true;
		}

		//список действий
		JSArray acts;
		bool bActsPrepared = false;
		if ((QSPIsActionsChanged() == QSP_TRUE) || Skin::isHtmlModeChanged)
		{
			int nActsCount = QSPGetActionsCount();
			for (int i = 0; i < nActsCount; i++)
			{
				QSP_CHAR* pImgPath;
				QSP_CHAR* pDesc;
				QSPGetActionData(i, &pImgPath, &pDesc);
				JSObject act;
				string imgPath = fromQsp(pImgPath);
				string desc = Skin::applyHtmlFixes(fromQsp(pDesc));
				act.SetProperty(WSLit("image"), ToWebString(imgPath));
				act.SetProperty(WSLit("desc"), ToWebString(desc));
				acts.Push(act);
			}
			bActsPrepared = true;
		}

		//инвентарь
		JSArray objs;
		bool bObjsPrepared = false;
		if ((QSPIsObjectsChanged() == QSP_TRUE) || Skin::isHtmlModeChanged)
		{
			int nObjsCount = QSPGetObjectsCount();
			int nSelectedObject = QSPGetSelObjectIndex();
			for (int i = 0; i < nObjsCount; i++)
			{
				QSP_CHAR* pImgPath;
				QSP_CHAR* pDesc;
				QSPGetObjectData(i, &pImgPath, &pDesc);
				JSObject obj;
				string imgPath = fromQsp(pImgPath);
				string desc = Skin::applyHtmlFixes(fromQsp(pDesc));
				int selected = (i == nSelectedObject) ? 1 : 0;
				obj.SetProperty(WSLit("image"), ToWebString(imgPath));
				obj.SetProperty(WSLit("desc"), ToWebString(desc));
				obj.SetProperty(WSLit("selected"), JSValue(selected));
				objs.Push(obj);
			}
			bObjsPrepared = true;
		}

		//доп. описание
		string varsDesc = "";
		bool bVarsDescPrepared = false;
		if ((QSPIsVarsDescChanged() == QSP_TRUE) || Skin::isHtmlModeChanged)
		{
			varsDesc = Skin::applyHtmlFixes(fromQsp(QSPGetVarsDesc()));
			bVarsDescPrepared = true;
		}

		// Яваскрипт, переданный из игры командой EXEC('JS:...')
		string jsCmd = "";
		bool bJsCmdPrepared = false;
		if (jsExecBuffer.length() > 0)
		{
			jsCmd = jsExecBuffer;
			jsExecBuffer = "";
			bJsCmdPrepared = true;
		}

		// Передаем собранные данные в яваскрипт
		if (bSkinPrepared || bMainDescPrepared || bActsPrepared || bObjsPrepared || bVarsDescPrepared ||
			bJsCmdPrepared)
		{
			JSObject groupedContent;
			if (bSkinPrepared)
				groupedContent.SetProperty(WSLit("skin"), jsSkin);
			if (bMainDescPrepared)
				groupedContent.SetProperty(WSLit("main"), ToWebString(mainDesc));
			if (bActsPrepared)
				groupedContent.SetProperty(WSLit("acts"), acts);
			if (bVarsDescPrepared)
				groupedContent.SetProperty(WSLit("vars"), ToWebString(varsDesc));
			if (bObjsPrepared)
				groupedContent.SetProperty(WSLit("objs"), objs);
			if (bJsCmdPrepared)
				groupedContent.SetProperty(WSLit("js"), ToWebString(jsCmd));
			qspSetGroupedContent(groupedContent);
		}
		Skin::resetUpdate();
	}

	void QnApplicationListener::SetTimer(int msecs)
	{
		//  	//Контекст библиотеки
		//  	final int timeMsecs = msecs;
		//mainActivity.runOnUiThread(new Runnable() {
		//	public void run() {
		//		timerInterval = timeMsecs;
		//	}
		//});
	}

	void QnApplicationListener::ShowMessage(QSP_CHAR* message)
	{
		//  	//Контекст библиотеки
		//if (libThread==null)
		//{
		//	Utility.WriteLog("ShowMessage: failed, libThread is null");
		//	return;
		//}

		//   // Обновляем скин
		//      skin.updateBaseVars();
		//      skin.updateMsgDialog();
		//      skin.updateEffects();
		//   // Если что-то изменилось, то передаем в яваскрипт
		//   if (skin.isSomethingChanged() && (skin.disableAutoRef != 1))
		//   {
		//       Utility.WriteLog("Hey! Skin was changed! Updating it before showing MSG.");
		//       RefreshInt(QSP_TRUE);
		//   }
		//
		//String msgValue = "";
		//if (message != null)
		//	msgValue = message;
		//
		//dialogHasResult = false;

		//  	final String msg = skin.applyHtmlFixes(msgValue);
		//mainActivity.runOnUiThread(new Runnable() {
		//	public void run() {
		//		jsQspMsg(msg);
		//		Utility.WriteLog("ShowMessage(UI): dialog showed");
		//	}
		//});
		//  	
		//Utility.WriteLog("ShowMessage: parking library thread");
		//      while (!dialogHasResult) {
		//      	setThreadPark();
		//      }
		//      parkThread = null;
		//Utility.WriteLog("ShowMessage: library thread unparked, finishing");
	}

	void QnApplicationListener::PlayFile(QSP_CHAR* file, int volume)
	{
		//  	//Контекст библиотеки
		//  	file = Utility.QspPathTranslate(file);
		//  	
		//  	if (file == null || file.length() == 0)
		//  	{
		//  		Utility.WriteLog("ERROR - filename is " + (file == null ? "null" : "empty"));
		//  		return;
		//  	}
		//  	
		//  	//Проверяем, проигрывается ли уже этот файл.
		//  	//Если проигрывается, ничего не делаем.
		//  	if (CheckPlayingFileSetVolume(file, true, volume))
		//  		return;

		//  	// Добавляем к имени файла полный путь
		//  	String prefix = "";
		//  	if (curGameDir != null)
		//  		prefix = curGameDir;
		//  	String assetFilePath = prefix.concat(file);

		//  	//Проверяем, существует ли файл.
		////Если нет, ничего не делаем.
		//      if (!Utility.CheckAssetExists(uiContext, assetFilePath, "PlayFile"))
		//      	return;

		//  	AssetManager assetManager = uiContext.getAssets();
		//  	if (assetManager == null)
		//  	{
		//  		Utility.WriteLog("PlayFile: failed, assetManager is null");
		//  		return;
		//  	}
		//  	AssetFileDescriptor afd = null;
		//  	try {
		//	afd = assetManager.openFd(assetFilePath);
		//} catch (IOException e) {
		//	e.printStackTrace();
		//	return;
		//}
		//  	if (afd == null)
		//  		return;
		//      
		//  	MediaPlayer mediaPlayer = new MediaPlayer();
		//   try {
		//	mediaPlayer.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
		//} catch (IllegalArgumentException e) {
		//	e.printStackTrace();
		//	return;
		//} catch (IllegalStateException e) {
		//	e.printStackTrace();
		//	return;
		//} catch (IOException e) {
		//	e.printStackTrace();
		//	return;
		//}
		//   try {
		//	mediaPlayer.prepare();
		//} catch (IllegalStateException e) {
		//	e.printStackTrace();
		//	return;
		//} catch (IOException e) {
		//	e.printStackTrace();
		//	return;
		//}
		//final String fileName = file;
		//mediaPlayer.setOnCompletionListener(new MediaPlayer.OnCompletionListener() {
		//	@Override
		//	public void onCompletion(MediaPlayer mp) {
		//        musicLock.lock();
		//        try {
		//	    	for (int i=0; i<mediaPlayersList.size(); i++)
		//	    	{
		//	    		ContainerMusic it = mediaPlayersList.elementAt(i);    		
		//	    		if (it.path.equals(fileName))
		//	    		{
		//	    			it.player.release();
		//	    			it.player = null;
		//	    			mediaPlayersList.remove(it);
		//	    			break;
		//	    		}
		//	    	}
		//        } finally {
		//        	musicLock.unlock();
		//        }
		//	}
		//});
		//
		//      musicLock.lock();
		//      try {
		//	float realVolume = GetRealVolume(volume);
		//	mediaPlayer.setVolume(realVolume, realVolume);
		//    mediaPlayer.start();
		//    ContainerMusic ContainerMusic = new ContainerMusic();
		//    ContainerMusic.path = file;
		//    ContainerMusic.volume = volume;
		//    ContainerMusic.player = mediaPlayer;
		//      	mediaPlayersList.add(ContainerMusic);
		//      } finally {
		//      	musicLock.unlock();
		//      }
	}

	QSP_BOOL QnApplicationListener::IsPlayingFile(QSP_CHAR* file)
	{
		////Контекст библиотеки
		//return CheckPlayingFileSetVolume(Utility.QspPathTranslate(file), false, 0);

		return QSP_FALSE;
	}

	void QnApplicationListener::CloseFile(QSP_CHAR* file)
	{
		////Контекст библиотеки
		//file = Utility.QspPathTranslate(file);
		//
		////Если вместо имени файла пришел null, значит закрываем все файлы(CLOSE ALL)
		//bool bCloseAll = false;
		//if (file == null)
		//	bCloseAll = true;
		//else if (file.length() == 0)
		//	return;
		//   musicLock.lock();
		//   try {
		//	for (int i=0; i<mediaPlayersList.size(); i++)
		//	{
		//		ContainerMusic it = mediaPlayersList.elementAt(i);    		
		//		if (bCloseAll || it.path.equals(file))
		//		{
		//			if (it.player.isPlaying())
		//				it.player.stop();
		//			it.player.release();
		//			it.player = null;
		//			if (!bCloseAll)
		//			{
		//				mediaPlayersList.remove(it);
		//				break;
		//			}
		//		}
		//	}
		//	if (bCloseAll)
		//		mediaPlayersList.clear();
		//   } finally {
		//   	musicLock.unlock();
		//   }
	}

	void QnApplicationListener::ShowPicture(QSP_CHAR* file)
	{
		//  	//Контекст библиотеки
		//  	if (file == null)
		//  		file = "";
		//  	
		//  	final String fileName = Utility.QspPathTranslate(file);
		//  	
		//mainActivity.runOnUiThread(new Runnable() {
		//	public void run() {
		//		if (fileName.length() > 0)
		//		{
		//	    	String prefix = "";
		//	    	if (curGameDir != null)
		//	    		prefix = curGameDir;
		//	    	
		//	    	//Проверяем, существует ли файл.
		//	    	//Если нет - выходим
		//	        if (!Utility.CheckAssetExists(uiContext, prefix.concat(fileName), "ShowPicture"))
		//	        	return;
		//		}
		//        // "Пустое" имя файла тоже имеет значение - так мы скрываем картинку
		//        jsQspView(fileName);
		//	}
		//});    	    	
	}

	void QnApplicationListener::InputBox(const QSP_CHAR* prompt, QSP_CHAR* buffer, int maxLen)
	{
		//  	//Контекст библиотеки
		//if (libThread==null)
		//{
		//	Utility.WriteLog("InputBox: failed, libThread is null");
		//	return "";
		//}

		//   // Обновляем скин
		//   skin.updateBaseVars();
		//   skin.updateInputDialog();
		//   skin.updateEffects();
		//   // Если что-то изменилось, то передаем в яваскрипт
		//   if (skin.isSomethingChanged() && (skin.disableAutoRef != 1))
		//   {
		//       Utility.WriteLog("Hey! Skin was changed! Updating it before showing INPUT.");
		//       RefreshInt(QSP_TRUE);
		//   }
		//
		//String promptValue = "";
		//if (prompt != null)
		//	promptValue = prompt;
		//
		//dialogHasResult = false;
		//
		//  	final String inputboxTitle = skin.applyHtmlFixes(promptValue);
		//mainActivity.runOnUiThread(new Runnable() {
		//	public void run() {
		//		inputboxResult = "";
		//		jsQspInput(inputboxTitle);
		//		Utility.WriteLog("InputBox(UI): dialog showed");
		//	}
		//});
		//  	
		//Utility.WriteLog("InputBox: parking library thread");
		//      while (!dialogHasResult) {
		//      	setThreadPark();
		//      }
		//      parkThread = null;
		//Utility.WriteLog("InputBox: library thread unparked, finishing");
		//  	return inputboxResult;


		//wcsncpy(buffer, dialog.GetText().c_str(), maxLen);
	}


	// Функция запросов к плееру.
	// С помощью этой функции мы можем в игре узнать параметры окружения плеера.
	// Вызывается так: $platform = GETPLAYER('platform')
	// @param resource
	// @return

	void QnApplicationListener::PlayerInfo(QSP_CHAR* resource, QSP_CHAR* buffer, int maxLen)
	{
		////Контекст библиотеки
		//resource = resource.toLowerCase();
		//if (resource.equals("platform")) {
		//	return "Android";
		//} else if (resource.equals("player")) {
		//	return "Quest Navigator";
		//} else if (resource.equals("player.version")) {
		//	return "1.0.0";
		//}
		//return "";


		//wcsncpy(buffer, dialog.GetText().c_str(), maxLen);
	}

	int QnApplicationListener::GetMSCount()
	{
		////Контекст библиотеки
		//return (int) (System.currentTimeMillis() - gameStartTime);

		return 0;
	}

	void QnApplicationListener::AddMenuItem(QSP_CHAR* name, QSP_CHAR* imgPath)
	{
		////Контекст библиотеки
		//ContainerMenuItem item = new ContainerMenuItem();
		//item.imgPath = Utility.QspPathTranslate(imgPath);
		//item.name = name;
		//menuList.add(item);
	}

	int QnApplicationListener::ShowMenu()
	{
		//  	//Контекст библиотеки
		//if (libThread==null)
		//{
		//	Utility.WriteLog("ShowMenu: failed, libThread is null");
		//	return -1;
		//}

		//   // Обновляем скин
		//   skin.updateMenuDialog();
		//   skin.updateEffects();
		//   // Если что-то изменилось, то передаем в яваскрипт
		//   if (skin.isSomethingChanged() && (skin.disableAutoRef != 1))
		//   {
		//       Utility.WriteLog("Hey! Skin was changed! Updating it before showing user menu.");
		//       RefreshInt(QSP_TRUE);
		//   }
		//
		//dialogHasResult = false;
		//menuResult = -1;

		//final Vector<ContainerMenuItem> uiMenuList = menuList; 
		//  	
		//mainActivity.runOnUiThread(new Runnable() {
		//	public void run() {
		//        JSONArray jsonMenuList = new JSONArray();
		//		for (int i = 0; i < uiMenuList.size(); i++)
		//		{
		//			JSONObject jsonMenuItem = new JSONObject();
		//			try {
		//				jsonMenuItem.put("image", uiMenuList.elementAt(i).imgPath);
		//				jsonMenuItem.put("desc", skin.applyHtmlFixes(uiMenuList.elementAt(i).name));
		//				jsonMenuList.put(i, jsonMenuItem);
		//			} catch (JSONException e) {
		//	    		Utility.WriteLog("ERROR - jsonMenuItem or jsonMenuList[] in ShowMenu!");
		//				e.printStackTrace();
		//			}
		//		}
		//		jsQspMenu(jsonMenuList);
		//	    Utility.WriteLog("ShowMenu(UI): dialog showed");
		//	}
		//});
		//  	
		//Utility.WriteLog("ShowMenu: parking library thread");
		//      while (!dialogHasResult) {
		//      	setThreadPark();
		//      }
		//      parkThread = null;
		//Utility.WriteLog("ShowMenu: library thread unparked, finishing");
		//  	
		//return menuResult;

		return 0;
	}

	void QnApplicationListener::DeleteMenu()
	{
		////Контекст библиотеки
		//menuList.clear();
	}

	void QnApplicationListener::Wait(int msecs)
	{
		//  	//Контекст библиотеки
		//  	try {
		//	Thread.sleep(msecs);
		//} catch (InterruptedException e) {
		//	Utility.WriteLog("WAIT in library thread was interrupted");
		//	e.printStackTrace();
		//}
	}

	void QnApplicationListener::ShowWindow(int type, QSP_BOOL isShow)
	{
		//// Контекст библиотеки
		//skin.showWindow(type, isShow);
	}

	void QnApplicationListener::System(QSP_CHAR* cmd)
	{
		//Контекст библиотеки
		string jsCmd = fromQsp(cmd);
		string jsCmdUpper = toUpper(jsCmd);
		if (startsWith(jsCmdUpper, "JS:"))
		{
			jsCmd = jsCmd.substr(string("JS:").length());
    		// Сохраняем яваскрипт, переданный из игры командой EXEC('JS:...')
    		// На выполнение отдаём при обновлении интерфейса
    		jsExecBuffer = jsExecBuffer + jsCmd;
		}
	}

	// ********************************************************************
	// ********************************************************************
	// ********************************************************************
	//                        Вызовы JS-функций
	// ********************************************************************
	// ********************************************************************
	// ********************************************************************

	void QnApplicationListener::processLibJsCall()
	{
		// Контекст Ui
		string name = "";
		JSValue arg;
		lockData();
		name = g_sharedData.str;
		arg = g_sharedData.jsValue;
		unlockData();
		jsCallApiFromUi(name, arg);
		runSyncEvent(evJsExecuted);
	}

	void QnApplicationListener::jsCallApiFromUi(string name, JSValue arg)
	{
		// Контекст Ui
		JSValue window = view_->web_view()->ExecuteJavascriptWithResult(
			WSLit("window"), WSLit(""));
		if (window.IsObject()) {
			JSArray args;
			args.Push(arg);
			window.ToObject().Invoke(ToWebString(name), args);
		} else {
			showError("Не удалось получить доступ к объекту окна.");
		}
	}
	void QnApplicationListener::jsCallApiFromLib(string name, JSValue arg)
	{
		// Контекст библиотеки
		// Вызвать функции Awesomium можно только из Ui-потока.
		
		// Отправляем данные в Ui-поток.
		lockData();
		g_sharedData.str = name;
		g_sharedData.jsValue = arg;
		runSyncEvent(evJsCommitted);
		unlockData();

		// Дожидаемся ответа, что JS-запрос выполнен.
		waitForSingle(evJsExecuted);
	}
	void QnApplicationListener::qspSetGroupedContent(JSObject content)
	{
		jsCallApiFromLib("qspSetGroupedContent", content);
	}
	void QnApplicationListener::qspShowSaveSlotsDialog(JSObject content)
	{
		jsCallApiFromLib("qspShowSaveSlotsDialog", content);
	}
	void QnApplicationListener::qspMsg(WebString text)
	{
		jsCallApiFromLib("qspMsg", text);
	}
	void QnApplicationListener::qspError(WebString error)
	{
		jsCallApiFromLib("qspError", error);
	}
	void QnApplicationListener::qspMenu(JSObject menu)
	{
		jsCallApiFromLib("qspMenu", menu);
	}
	void QnApplicationListener::qspInput(WebString text)
	{
		jsCallApiFromLib("qspInput", text);
	}
	void QnApplicationListener::qspView(WebString path)
	{
		jsCallApiFromLib("qspView", path);
	}





	// ********************************************************************
	// ********************************************************************
	// ********************************************************************
	//                   Обработчики вызовов из JS
	// ********************************************************************
	// ********************************************************************
	// ********************************************************************

	void QnApplicationListener::restartGame(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		string gameFile = Configuration::getString(ecpGameFile);
		StopGame(true);
		runGame(gameFile);
	}

	void QnApplicationListener::executeAction(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		if (args.size() < 1) {
			showError("Не указан параметр для executeAction!");
			return;
		}
		JSValue jsPos = args[0];
		int pos = jsPos.ToInteger();
		lockData();
		g_sharedData.num = pos;
		runSyncEvent(evExecuteAction);
		unlockData();
	}

	void QnApplicationListener::selectObject(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		if (args.size() < 1) {
			showError("Не указан параметр для selectObject!");
			return;
		}
		JSValue jsPos = args[0];
		int pos = jsPos.ToInteger();
		lockData();
		g_sharedData.num = pos;
		runSyncEvent(evSelectObject);
		unlockData();
	}

	void QnApplicationListener::loadGame(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		app_->ShowMessage("game load dialog need to be opened!");
	}

	void QnApplicationListener::saveGame(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		app_->ShowMessage("game save dialog need to be opened!");
	}

	void QnApplicationListener::saveSlotSelected(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		app_->ShowMessage("slot selected!");
	}

	void QnApplicationListener::msgResult(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		app_->ShowMessage("msg dialog closed!");
	}

	void QnApplicationListener::errorResult(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		app_->ShowMessage("error dialog closed!");
	}

	void QnApplicationListener::userMenuResult(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		app_->ShowMessage("user menu dialog closed!");
	}

	void QnApplicationListener::inputResult(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		app_->ShowMessage("input dialog closed!!");
	}

	void QnApplicationListener::setMute(WebView* caller, const JSArray& args)
	{
		// Контекст UI
		app_->ShowMessage("mute called!");
	}

	// ********************************************************************
	// Вспомогательные обработчики для отладки
	// ********************************************************************
	void QnApplicationListener::alert(WebView* caller, const JSArray& args)
	{
		string msg = "";
		if (args.size() > 0) {
			msg = ToString(args[0].ToString());
		}
		showMessage(msg, "");
	}

	// ********************************************************************
	// Переменные библиотеки
	// ********************************************************************

	string QnApplicationListener::jsExecBuffer = "";
	QnApplicationListener* QnApplicationListener::listener = NULL;

	//******************************************************************************
	//******************************************************************************
	//****** / THREADS \ ***********************************************************
	//******************************************************************************
	//******************************************************************************

	// Все функции библиотеки QSP (QSPInit и т.д.) 
	// вызываются только внутри потока библиотеки.

	// Создаём объект ядра для синхронизации потоков,
	// событие с автосбросом, инициализированное в занятом состоянии.
	HANDLE CreateSyncEvent()
	{
		HANDLE eventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (eventHandle == NULL) {
			showError("Не получилось создать объект ядра \"событие\" для синхронизации потоков.");
			exit(0);
		}
		return eventHandle;
	}

	// Получаем HANDLE события по его индексу
	HANDLE getEventHandle(eSyncEvent ev)
	{
		return g_eventList[ev];
	}

	// Запускаем событие
	void runSyncEvent(eSyncEvent ev)
	{
		BOOL res = SetEvent(getEventHandle(ev));
		if (res == 0) {
			showError("Не удалось запустить событие синхронизации потоков.");
			exit(0);
		}
	}

	// Высвобождаем описатель и ругаемся если что не так.
	void freeHandle(HANDLE handle)
	{
		BOOL res = CloseHandle(handle);
		if (res == 0) {
			showError("Не удалось высвободить описатель объекта ядра.");
			exit(0);
		}
	}

	// Входим в критическую секцию
	void lockData()
	{
		try {
			EnterCriticalSection(&g_csSharedData);
		} catch (...) {
			showError("Не удалось войти в критическую секцию.");
			exit(0);
		}
	}

	// Выходим из критической секции
	void unlockData()
	{
		LeaveCriticalSection(&g_csSharedData);
	}

	// Ожидаем события
	bool waitForSingle(HANDLE handle)
	{
		DWORD res = WaitForSingleObject(handle, INFINITE);
		if (res != WAIT_OBJECT_0) {
			showError("Не удалось дождаться события синхронизации");
			return false;
		}
		return true;
	}
	bool waitForSingle(eSyncEvent ev)
	{
		return waitForSingle(getEventHandle(ev));
	}
	bool checkForSingle(eSyncEvent ev)
	{
		HANDLE handle = getEventHandle(ev);
		DWORD res = WaitForSingleObject(handle, 0);
		if ((res == WAIT_ABANDONED) || (res == WAIT_FAILED)) {
			showError("Сбой синхронизации");
			return false;
		}
		return res == WAIT_OBJECT_0;
	}

	// Запуск потока библиотеки. Вызывается только раз при старте программы.
	void QnApplicationListener::StartLibThread()
	{
		//Utility.WriteLog("StartLibThread: enter ");    	
		//Контекст UI
		if (libThread != NULL)
		{
			//		Utility.WriteLog("StartLibThread: failed, libThread is not null");    	
			showError("StartLibThread: failed, libThread is not null");
			exit(0);
			return;
		}

		// Инициализируем объекты синхронизации.
		// Все используемые объекты - события с автосбросом, 
		// инициализированные в занятом состоянии.
		for (int i = 0; i < (int)evLast; i++) {
			HANDLE eventHandle = CreateSyncEvent();
			if (eventHandle == NULL)
				return;
			g_eventList[i] = eventHandle;
		}
		// Инициализируем структуру критической секции
		try {
			InitializeCriticalSection(&g_csSharedData);
		} catch (...) {
			showError("Не удалось проинициализировать структуру критической секции.");
			exit(0);
			return;
		}


		libThread = (HANDLE)_beginthreadex(NULL, 0, &QnApplicationListener::libThreadFunc, this, 0, NULL);
		if (libThread == NULL) {
			showError("Не получилось создать поток интерпретатора.");
			exit(0);
			return;
		}

		//// Готовим данные для передачи в поток
		//lockData();
		//g_sharedData.str = "abc";
		//runSyncEvent(evTest);
		//unlockData();

		//// Ждём возврата данных из потока
		//string blabla = "";
		//waitForSingle(evCommitted);
		//lockData();
		//blabla = g_sharedData.str;
		//unlockData();
		//	showMessage(blabla, "");





		//final QspLib pluginObject = this; 

		////Запускаем поток библиотеки
		//Thread t = new Thread() {
		//	public void run() {
		//		Looper.prepare();
		//		libThreadHandler = new Handler();
		//		Utility.WriteLog("LibThread runnable: libThreadHandler is set");    	

		//		libThreadHandler.post(new Runnable() {
		//			public void run() {
		//				// Создаем скин
		//				skin = new QspSkin(pluginObject);

		//				//Создаем список для звуков и музыки
		//				musicLock.lock();
		//				try {
		//					mediaPlayersList = new Vector<ContainerMusic>();
		//					muted = false;
		//				} finally {
		//					musicLock.unlock();
		//				}

		//				// Сообщаем яваскрипту, что библиотека запущена
		//				// и можно продолжить инициализацию
		//				mainActivity.runOnUiThread(new Runnable() {
		//					public void run() {
		//						jsInitNext();
		//					}
		//				});
		//			}
		//		});

		//		Looper.loop();
		//		Utility.WriteLog("LibThread runnable: library thread exited");
		//	}
		//};
		//libThread = t;
		//t.start();
		//Utility.WriteLog("StartLibThread: success");
	}

	// Остановка потока библиотеки. Вызывается только раз при завершении программы.
	void QnApplicationListener::StopLibThread()
	{
		//Utility.WriteLog("StopLibThread: enter");    	
		////Контекст UI
		////Останавливаем поток библиотеки
		//libThreadHandler.getLooper().quit();

		// Сообщаем потоку библиотеки, что нужно завершить работу
		runSyncEvent(evShutdown);
		// Ждём завершения библиотечного потока
		waitForSingle(libThread);
		// Закрываем хэндл библиотечного потока
		freeHandle(libThread);
		libThread = NULL;
		// Закрываем хэндлы событий
		for (int i = 0; i < (int)evLast; i++) {
			freeHandle(g_eventList[i]);
		}
		// Высвобождаем структуру критической секции
		DeleteCriticalSection(&g_csSharedData);
		//Utility.WriteLog("StopLibThread: success");    	
	}

	// Основная функция потока библиотеки. Вызывается только раз за весь жизненный цикл программы.
	unsigned int QnApplicationListener::libThreadFunc(void* pvParam)
	{
		// Сохраняем указатель на listener
		listener = (QnApplicationListener*) pvParam;

		// Инициализируем библиотеку
		QSPInit();

		// Привязываем колбэки
		QSPSetCallBack(QSP_CALL_REFRESHINT, (QSP_CALLBACK)&RefreshInt);
		//QSPSetCallBack(QSP_CALL_SETTIMER, (QSP_CALLBACK)&SetTimer);
		//QSPSetCallBack(QSP_CALL_SETINPUTSTRTEXT, (QSP_CALLBACK)&SetInputStrText);
		//QSPSetCallBack(QSP_CALL_ISPLAYINGFILE, (QSP_CALLBACK)&IsPlay);
		//QSPSetCallBack(QSP_CALL_PLAYFILE, (QSP_CALLBACK)&PlayFile);
		//QSPSetCallBack(QSP_CALL_CLOSEFILE, (QSP_CALLBACK)&CloseFile);
		//QSPSetCallBack(QSP_CALL_SHOWMSGSTR, (QSP_CALLBACK)&Msg);
		//QSPSetCallBack(QSP_CALL_SLEEP, (QSP_CALLBACK)&Sleep);
		//QSPSetCallBack(QSP_CALL_GETMSCOUNT, (QSP_CALLBACK)&GetMSCount);
		//QSPSetCallBack(QSP_CALL_DELETEMENU, (QSP_CALLBACK)&DeleteMenu);
		//QSPSetCallBack(QSP_CALL_ADDMENUITEM, (QSP_CALLBACK)&AddMenuItem);
		//QSPSetCallBack(QSP_CALL_SHOWMENU, (QSP_CALLBACK)&ShowMenu);
		//QSPSetCallBack(QSP_CALL_INPUTBOX, (QSP_CALLBACK)&Input);
		//QSPSetCallBack(QSP_CALL_SHOWIMAGE, (QSP_CALLBACK)&ShowImage);
		//QSPSetCallBack(QSP_CALL_SHOWWINDOW, (QSP_CALLBACK)&ShowPane);
		QSPSetCallBack(QSP_CALL_SYSTEM, (QSP_CALLBACK)&System);
		//QSPSetCallBack(QSP_CALL_OPENGAMESTATUS, (QSP_CALLBACK)&OpenGameStatus);
		//QSPSetCallBack(QSP_CALL_SAVEGAMESTATUS, (QSP_CALLBACK)&SaveGameStatus);

		// Заполняем значения по умолчанию для скина
		Skin::initDefaults();



		// Обработка событий происходит в цикле
		bool bShutdown = false;
		while (!bShutdown) {
			// Ожидаем любое из событий синхронизации
			DWORD res = WaitForMultipleObjects((DWORD)evLastUi, g_eventList, FALSE, INFINITE);
			if ((res < WAIT_OBJECT_0) || (res > (WAIT_OBJECT_0 + evLast - 1))) {
				showError("Не удалось дождаться события синхронизации.");
				bShutdown = true;
			} else {
				eSyncEvent ev = (eSyncEvent)res;
				switch (ev)
				{
				case evRunGame:
					{
						// Запуск игры
						string path = "";
						lockData();
						path = g_sharedData.str;
						unlockData();
						QSP_BOOL res = QSPLoadGameWorld(widen(path).c_str());
						CheckQspResult(res, "QSPLoadGameWorld");
						// Очищаем скин
						Skin::resetUpdate();
						Skin::resetSettings();
						// Очищаем буфер JS-команд, передаваемых из игры
						jsExecBuffer = "";
						//	            //Запускаем таймер
						//	            timerInterval = 500;
						//	            timerStartTime = System.currentTimeMillis();
						//	            timerHandler.removeCallbacks(timerUpdateTask);
						//	            timerHandler.postDelayed(timerUpdateTask, timerInterval);
						//	            
						//	            //Запускаем счетчик миллисекунд
						//	            gameStartTime = System.currentTimeMillis();

						//				//Создаем список для звуков и музыки
						//				musicLock.lock();
						//				try {
						//					mediaPlayersList = new Vector<ContainerMusic>();
						//					muted = false;
						//				} finally {
						//					musicLock.unlock();
						//				}

						//	            //Все готово, запускаем игру
						//	            libThreadHandler.post(new Runnable() {
						//	        		public void run() {
						//	                	libraryThreadIsRunning = true;
						//	        			boolean result = QSPRestartGame(true);
						//CheckQspResult(result, "runGame: QSPRestartGame");
						//	                	libraryThreadIsRunning = false;
						//	        		}
						//	            });

						//	            
						//	            gameIsRunning = true;

						res = QSPRestartGame(QSP_TRUE);
						CheckQspResult(res, "QSPRestartGame");
					}
					break;
				case evStopGame:
					{
						// Остановка игры
						//	//останавливаем таймер
						//	timerHandler.removeCallbacks(timerUpdateTask);

						//	//останавливаем музыку
						//	libThreadHandler.post(new Runnable() {
						//		public void run() {
						//			CloseFile(null);
						//		}
						//	});
						// Очищаем буфер JS-команд, передаваемых из игры
						jsExecBuffer = "";

						runSyncEvent(evGameStopped);
					}
					break;
				case evShutdown:
					{
						// Завершение работы
						bShutdown = true;
					}
					break;
				case evExecuteCode:
					{
						// Выполнение строки кода
						string code = "";
						lockData();
						code = g_sharedData.str;
						unlockData();
						wstring wCode = widen(code);
						QSP_BOOL res = QSPExecString(wCode.c_str(), QSP_TRUE);
						CheckQspResult(res, "QSPExecString");
					}
					break;
				case evExecuteAction:
					{
						// Выполнение действия
						int pos = 0;
						lockData();
						pos = g_sharedData.num;
						unlockData();
						QSP_BOOL res = QSPSetSelActionIndex(pos, QSP_FALSE);
						CheckQspResult(res, "QSPSetSelActionIndex");
						res = QSPExecuteSelActionCode(QSP_TRUE);
						CheckQspResult(res, "QSPExecuteSelActionCode");
					}
					break;
				case evSelectObject:
					{
						// Выбор предмета
						int pos = 0;
						lockData();
						pos = g_sharedData.num;
						unlockData();
        				QSP_BOOL res = QSPSetSelObjectIndex(pos, QSP_TRUE);
						CheckQspResult(res, "QSPSetSelObjectIndex");
					}
					break;
				default:
					{
						showError("Необработанное событие синхронизации!");
						bShutdown = true;
					}
					break;
				}
			}
		}

		// Завершаем работу библиотеки
		QSPDeInit();
		// Завершаем работу потока
		_endthreadex(0);
		return 0;
	}
	//******************************************************************************
	//******************************************************************************
	//****** \ THREADS / ***********************************************************
	//******************************************************************************
	//******************************************************************************

	// Проверяем статус выполнения операции и сообщаем об ошибке, если требуется.
	void QnApplicationListener::CheckQspResult(QSP_BOOL successfull, string failMsg)
	{
		//Контекст библиотеки
		if (successfull == QSP_FALSE)
		{
			showError(failMsg + " failed.");

			//     	//Контекст библиотеки
			// 		Utility.WriteLog(failMsg + " failed");

			// 		ContainerJniResult error = (ContainerJniResult) QSPGetLastErrorData();
			//error.str2 = QSPGetErrorDesc(error.int1);
			//  	String locName = skin.applyHtmlFixes((error.str1 == null) ? "" : error.str1);
			//  	String errDesc = skin.applyHtmlFixes((error.str2 == null) ? "" : error.str2);

			// 		if (libThread==null)
			// 		{
			// 			Utility.WriteLog("CheckQspResult: failed, libThread is null");
			// 			return;
			// 		}

			// 	    // Обновляем скин
			//         skin.updateBaseVars();
			//         skin.updateMsgDialog();
			//         skin.updateEffects();
			// 	    // Если что-то изменилось, то передаем в яваскрипт
			// 	    if (skin.isSomethingChanged() && (skin.disableAutoRef != 1))
			// 	    {
			// 	        Utility.WriteLog("Hey! Skin was changed! Updating it before showing error dialog.");
			// 	        RefreshInt(QSP_TRUE);
			// 	    }
			// 		
			// 		dialogHasResult = false;
			// 		
			//     	final JSONObject jsErrorContainer = new JSONObject();
			//     	try {
			//              jsErrorContainer.put("desc", errDesc);
			//              jsErrorContainer.put("loc", locName);
			//              jsErrorContainer.put("actIndex", error.int2);
			//              jsErrorContainer.put("line", error.int3);
			//} catch (JSONException e) {
			//  		Utility.WriteLog("ERROR - jsErrorContainer in CheckQspResult!");
			//	e.printStackTrace();
			//}
			// 		
			// 		mainActivity.runOnUiThread(new Runnable() {
			// 			public void run() {
			// 				jsQspError(jsErrorContainer);
			// 				Utility.WriteLog("CheckQspResult(UI): error dialog showed");
			// 			}
			// 		});
			//     	
			// 		Utility.WriteLog("CheckQspResult: parking library thread");
			//         while (!dialogHasResult) {
			//         	setThreadPark();
			//         }
			//         parkThread = null;
			// 		Utility.WriteLog("CheckQspResult: library thread unparked, finishing");
		}
	}

}