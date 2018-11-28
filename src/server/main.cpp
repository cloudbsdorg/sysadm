// ===============================
//  PC-BSD REST API Server
// Available under the 3-clause BSD License
// Written by: Ken Moore <ken@pcbsd.org> July 2015
// =================================
#include "globals.h"

#include <unistd.h>
#include <sys/types.h>

#include "WebServer.h"

#define CONFFILE "/usr/local/etc/sysadm.conf"
#define SETTINGSFILE "/var/db/sysadm.ini"

#define DEBUG 0

//Create any global classes
QSettings *CONFIG = new QSettings(SETTINGSFILE, QSettings::IniFormat);
EventWatcher *EVENTS = new EventWatcher();
Dispatcher *DISPATCHER = new Dispatcher();
bool WS_MODE = false;

//Set the defail values for the global config variables
int BlackList_BlockMinutes = 60;
int BlackList_AuthFailsToBlock = 5;
int BlackList_AuthFailResetMinutes = 10;
bool BRIDGE_ONLY = false;

//Create the default logfile
QFile logfile;
void MessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg){
  QString txt;
  switch(type){
  case QtDebugMsg:
  	  txt = msg;
  	  break;
  case QtWarningMsg:
  	  txt = QString("WARNING: %1").arg(msg);
  	  break;
  case QtCriticalMsg:
  	  txt = QString("CRITICAL: %1").arg(msg);
  	  break;
  case QtFatalMsg:
  	  txt = QString("FATAL: %1").arg(msg);
  	  break;
  }
  if( type!=QtDebugMsg && !QString(context.file).isEmpty() ){
    txt += "\n Context: "+QString(context.file)+" Line: "+QString(context.line)+" Function: "+QString(context.function);
  }
  QTextStream out(&logfile);
  out << txt;
  if(!txt.endsWith("\n")){ out << "\n"; }
}

inline QString ReadFile(QString path){
  QFile file(path);
  if( !file.open(QIODevice::ReadOnly) ){ return ""; }
  QTextStream in(&file);
  QString str = in.readAll();
  file.close();
  return str;
}

void showUsage(){
qDebug() << "sysadm-binary usage:";
qDebug() << "Starting the server:";
qDebug() << "    \"sysadm-binary [-rest] [-port <portnumber>]\"";
qDebug() << "CLI flags for configuring the server:";
qDebug() << "  \"-h\" or \"help\": Show this help text";
qDebug() << "  \"import_ssl_file <username> <filepath> <nickname> [<email>]\": Loads a .crt or .key file and enables the public key for authorization access later";
qDebug() << "Configuring server->bridge connections (websockets only):";
qDebug() << "  \"bridge_list\": Show all bridges that are currently setup";
qDebug() << "  \"bridge_add <nickname> <url>\":  Create a new bridge connection with the given nickname";
qDebug() << "  \"bridge_remove <nickname>\": Remove the bridge connection with the given nickname";
qDebug() << "  \"bridge_export_key [file]\": Export the public SSL key the server uses to connect to bridges";
}

int main( int argc, char ** argv )
{
    //Check whether running as root
    if( getuid() != 0){
      qDebug() << "sysadm-server must be started as root!";
      return 1;
    }

    //Evaluate input arguments
    bool websocket = true;
    bool setonly = false;
    quint16 port = 0;
    for(int i=1; i<argc; i++){
      if( QString(argv[i])=="-rest" ){ websocket = false;}
      else if( QString(argv[i])=="-p" && (i+1<argc) ){ i++; port = QString(argv[i]).toUInt(); }
      else if( QString(argv[i])=="-h" || QString(argv[i]).contains("help") ){ showUsage(); return 0; }
      else if( QString(argv[i]).startsWith("bridge_") ){
        setonly = true;
        QString opt = QString(argv[i]).section("_",1,-1);
        if(opt=="list"){
          QStringList bridges = CONFIG->allKeys().filter("bridge_connections/");
          qDebug() << "Current Bridges:";
          for(int i=0; i<bridges.length(); i++){
            qDebug() << bridges[i].section("/",1,-1) + " ("+CONFIG->value(bridges[i]).toString()+")";
          }
        }else if(opt=="add" && argc > i+2){
          QString name = QString(argv[i+1]);
          QString url = QString(argv[i+2]);
          CONFIG->setValue("bridge_connections/"+name, url);
	  qDebug() << "New Bridge Added:" << name+" ("+url+")";
          i=i+2;
        }else if(opt=="remove" && argc>i+1){
          QString name = QString(argv[i+1]);
          CONFIG->remove("bridge_connections/"+name);
          qDebug() << "Bridge Removed:" << name;
          i=i+1;
        }else if(opt=="export_key"){
          //Export the public SSL cert used for establishing a connection with a bridge
          QFile cfile("/usr/local/etc/sysadm/ws_bridge.crt");
          if( cfile.open(QIODevice::ReadOnly) ){
            QSslCertificate cert(&cfile);
            cfile.close();
            if(!cert.isNull()){
              if(i+1<argc){
                i++; QString filepath = argv[i];
	        QFile outfile(filepath);
                  outfile.open(QIODevice::WriteOnly | QIODevice::Truncate);
                  outfile.write(cert.publicKey().toPem());
                outfile.close();
                qDebug() << "Public Key Saved to file:" << filepath;
              }else{
                //Output to std out instead
               qDebug() << cert.publicKey().toPem();
              }
            }
          }
        }else{
          qDebug() << "Unknown option:" << argv[i];
          return 1;
        }
      }else if(QString(argv[i])=="import_ssl_file" && i+3<argc){
        setonly = true;
        //Load CLI inputs
        i++; QString user(argv[i]); //username
        i++; QByteArray key(argv[i]); //key file
        i++; QString nickname(argv[i]); // nickname for key
        QString email; if(i+1<argc){ i++; email = QString(argv[i]); } //email address
        //Read the key file
        QFile file(key);
 	if(!file.open(QIODevice::ReadOnly)){ qDebug() << "Could not open file:" << file.fileName(); }
        else{
          QByteArray enc_key;
          if(file.fileName().endsWith(".crt")){
            QSslCertificate cert(&file, QSsl::Pem);
            if(!cert.isNull()){ enc_key = cert.publicKey().toPem(); }
          }else if(file.fileName().endsWith(".key")){
            QSslKey key( &file, QSsl::Rsa, QSsl::Pem, QSsl::PublicKey);
            if(!key.isNull()){ enc_key = key.toPem(); }
           }else{
             qDebug() << "Error: Unknown file type (need .crt or .key file)";
           }
          file.close();
          if(enc_key.isEmpty()){ qDebug() << "ERROR: Could not read file"; }
          else{
            bool ok = AuthorizationManager::RegisterCertificateInternal(user, enc_key, nickname, email);
            if(ok){ qDebug() << "Key Added" << user << nickname; }
            else{ qDebug() << "Could not add key"; }
          }
	}
	//See if the key is a file instead - then read it
        /*bool ok = true;
	if(QFile::exists(key)){
	  QFile file(key);
          QByteArray pubkey;
          if(file.open(QIODevice::ReadOnly)){
            QSslKey sslkey( &file, QSsl::Rsa, QSsl::Pem, QSsl::PublicKey);
            if(!key.isNull()){ pubkey = sslkey.toPem(); }
            else{ qDebug() << "Invalid Key file:" << file.fileName(); ok = false; }
            file.close();
          }else{ qDebug() << "Could not open file:" << file.fileName(); ok = false; }
        }
	if(ok){ ok = AuthorizationManager::RegisterCertificateInternal(user, key, nickname, email); }
	if(ok){ qDebug() << "Key Added" << user << nickname; }
        else{ qDebug() << "Could not add key"; } */

      }else{
        qDebug() << "Unknown option:" << argv[1];
        return 1;
      }
    }
    if(setonly){ CONFIG->sync(); return 0; }
    WS_MODE = websocket; //set the global variable too

    QCoreApplication a(argc, argv);
    //Now load the config file
    QString conf_file = CONFFILE;
    if( !QFile::exists(conf_file) ){ conf_file.append(".dist"); } //no settings - use the default config
    QStringList conf = ReadFile(conf_file).split("\n");
    if(!conf.filter("[internal]").isEmpty()){
      //Older QSettings file - move it to the new location
      if(QFile::exists(SETTINGSFILE)){ QFile::remove(SETTINGSFILE); } //remove the new/empty settings file
      QFile::copy(conf_file, SETTINGSFILE);
      CONFIG->sync(); //re-sync settings structure
      conf.clear(); //No config yet
    }
    //Load the settings from the config file
    // - port number
    if(port==0){
      if(websocket){
	int index = conf.indexOf(QRegExp("PORT=*",Qt::CaseSensitive,QRegExp::Wildcard));
	bool ok = false;
	if(index>=0){ port = conf[index].section("=",1,1).toInt(&ok); }
	if(port<=0 || !ok){ port = WSPORTNUMBER;  }
      }else{
	int index = conf.indexOf(QRegExp("PORT_REST=*",Qt::CaseSensitive,QRegExp::Wildcard));
	bool ok = false;
	if(index>=0){ port = conf[index].section("=",1,1).toInt(&ok); }
	if(port<=0 || !ok){ port = PORTNUMBER;  }
      }
    }
    // - Blacklist options
    QRegExp rg = QRegExp("BLACKLIST_BLOCK_MINUTES=*",Qt::CaseSensitive,QRegExp::Wildcard);
    if(!conf.filter(rg).isEmpty()){
      bool ok = false;
      int tmp = conf.filter(rg).first().section("=",1,1).simplified().toInt(&ok);
      if(ok){ BlackList_BlockMinutes = tmp; }
    }
    rg = QRegExp("BLACKLIST_AUTH_FAIL_LIMIT=*",Qt::CaseSensitive,QRegExp::Wildcard);
    if(!conf.filter(rg).isEmpty()){
      bool ok = false;
      int tmp = conf.filter(rg).first().section("=",1,1).simplified().toInt(&ok);
      if(ok){ BlackList_AuthFailsToBlock = tmp; }
    }
    rg = QRegExp("BLACKLIST_AUTH_FAIL_RESET_MINUTES=*",Qt::CaseSensitive,QRegExp::Wildcard);
    if(!conf.filter(rg).isEmpty()){
      bool ok = false;
      int tmp = conf.filter(rg).first().section("=",1,1).simplified().toInt(&ok);
      if(ok){ BlackList_AuthFailResetMinutes = tmp; }
    }
    rg = QRegExp("BRIDGE_CONNECTIONS_ONLY=*",Qt::CaseSensitive,QRegExp::Wildcard);
    if(!conf.filter(rg).isEmpty()){
      BRIDGE_ONLY = conf.filter(rg).first().section("=",1,1).simplified().toLower()=="true";
    }

    //Setup the log file
    LogManager::checkLogDir(); //ensure the logging directory exists
    if(!websocket){ logfile.setFileName("/var/log/sysadm-server-tcp.log"); }
    else{ logfile.setFileName("/var/log/sysadm-server-ws.log"); }
    if(DEBUG){ qDebug() << "Log File:" << logfile.fileName(); }
      if(QFile::exists(logfile.fileName()+".old")){ QFile::remove(logfile.fileName()+".old"); }
      if(logfile.exists()){ QFile::rename(logfile.fileName(), logfile.fileName()+".old"); }
      //Make sure the parent directory exists
      if(!QFile::exists("/var/log")){
        QDir dir;
        dir.mkpath("/var/log");
      }
      logfile.open(QIODevice::WriteOnly | QIODevice::Append);
      qInstallMessageHandler(MessageOutput);
    //Connect the background classes
    QObject::connect(DISPATCHER, SIGNAL(DispatchEvent(QJsonObject)), EVENTS, SLOT(DispatchEvent(QJsonObject)) );
    QObject::connect(DISPATCHER, SIGNAL(DispatchStarting(QString)), EVENTS, SLOT(DispatchStarting(QString)) );

    //Create the daemon
    qDebug() << "Starting the sysadm server...." << (websocket ? "(WebSocket)" : "(TCP)");
    WebServer *w = new WebServer();
    //Start the daemon
    int ret = 1; //error return value
    if( w->startServer(port, websocket) ){
      //qDebug() << " - Configuration File:" << CONFIG->fileName();
      QThread TBACK, TBACK2;
      EVENTS->moveToThread(&TBACK);
      DISPATCHER->moveToThread(&TBACK2);
      TBACK.start();
      TBACK2.start();
      QTimer::singleShot(0,EVENTS, SLOT(start()) );
      //Now start the main event loop
      ret = a.exec();
      qDebug() << "Server Stopped:" << QDateTime::currentDateTime().toString(Qt::ISODate);
      //TBACK.stop();
    }else{
      qDebug() << "[FATAL] Server could not be started:" << QDateTime::currentDateTime().toString(Qt::ISODate);
      qDebug() << " - Tried port:" << port;
    }
    //Cleanup any globals
    delete CONFIG;
    logfile.close();

    //Return
    return ret;
}
