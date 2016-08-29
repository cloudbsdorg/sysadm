#include "sysadm-servicemanager.h"
#include "sysadm-general.h"

#include <QFile>
#include <QDir>

using namespace sysadm;
ServiceManager::ServiceManager(QString chroot, QString ip)
{
    this->chroot = chroot;
    this->ip = ip;
    //loadServices();
}

Service ServiceManager::GetService(QString serviceName)
{
  if(!services.isEmpty()){
    for(int i=0; i<services.length(); i++){
        if(services[i].Name == serviceName)
            return services[i];
    }
  }else{
    return loadServices(serviceName);
  }
  return Service(); //no service found
}

QList<Service> ServiceManager::GetServices()
{
    if(services.isEmpty()){ loadServices(); }
    return services;
}

void ServiceManager::Start(Service service)
{
    // Start the process
    QString prog;
    QStringList args;

    if ( chroot.isEmpty() ) {
      prog = "service";
      args << service.Directory;
      args << "start";
    } else {
      prog = "warden";
      args << "chroot" << ip << "service" << service.Directory << "start";
    }
    General::RunCommand(prog,args);
}

void ServiceManager::Stop(Service service)
{
    // Start the process
    QString prog;
    QStringList args;

    if ( chroot.isEmpty() ) {
      prog = "service";
      args << service.Directory;
      args << "stop";
    } else {
      prog = "warden";
      args << "chroot" << ip << "service" << service.Directory << "stop";
    }
    General::RunCommand(prog,args);
}

void ServiceManager::Restart(Service service)
{
    QString prog;
    QStringList args;

    if ( chroot.isEmpty() ) {
      prog = "service";
      args << service.Directory;
      args << "restart";
    } else {
      prog = "warden";
      args << "chroot" << ip << "service" << service.Directory << "restart";
    }
    General::RunCommand(prog,args);
}

void ServiceManager::Enable(Service service)
{
    General::setConfFileValue( chroot + "/etc/rc.conf", service.Tag, service.Tag + "=\"YES\"", -1);
}

void ServiceManager::Disable(Service service)
{
    General::setConfFileValue( chroot + "/etc/rc.conf", service.Tag, service.Tag + "=\"NO\"", -1);
}

Service ServiceManager::loadServices(QString name)
{
    QString tmp;
    bool valid;
    Service service;

    QStringList stringDirs;
    stringDirs << chroot + "/etc/rc.d" << chroot + "/usr/local/etc/rc.d";

    for ( QString dir: stringDirs)
    {

        QDir directory( dir );

        directory.setFilter( QDir::Files );
        directory.setSorting( QDir::Name );

        if ( directory.count() == 0 )
            return Service();

        for (unsigned int i = 0; i < directory.count(); i++ )
        {
	    if(!name.isEmpty() && directory[i]!=name){ continue; } //not the right service - go to the next one
            service = Service();

            QFile file( dir + "/" + directory[i] );
            if ( file.open( QIODevice::ReadOnly ) )
            {
                valid=false;
                service.Directory=directory[i];
                QTextStream stream( &file );
                stream.setCodec("UTF-8");
                QString line;
                while ( !stream.atEnd() )
                {
                    line = stream.readLine(); // line of text excluding '\n'

                    if ( line.indexOf("name=") == 0)
                    {
                        valid=true;
                        tmp = line.replace("name=", "");
                        service.Name = tmp.replace('"', "");
                    }
                    if ( line.indexOf("rcvar=") == 0)
                    {
                        if ( tmp.isEmpty() )
                            continue;

                        tmp = line.replace("rcvar=", "");
                        tmp = tmp.replace('"', "");
                        tmp = tmp.replace("'", "");
                        tmp = tmp.replace("`", "");
                        tmp = tmp.replace("$(set_rcvar)", "");
                        tmp = tmp.replace("$set_rcvar", "");
                        tmp = tmp.replace("set_rcvar", "");
                        tmp = tmp.replace("${name}", "");
                        tmp = tmp.replace("_enable", "");
                        tmp = tmp.replace(" ", "");

                        if (tmp.isEmpty())
                            service.Tag = service.Name + "_enable";
                        else
                            service.Tag = tmp;

                        if ( service.Tag.indexOf("_enable") == -1 )
                            service.Tag=service.Tag + "_enable";
                        break;
                    }
                }
                file.close();

                if ( !valid || service.Tag.isEmpty() )
                    continue;

                QString cDir = dir;
                if ( ! chroot.isEmpty() )
                    cDir.replace(chroot, "");
                if ( service.Tag.indexOf("$") == 0 )
                    service.Tag = service.Directory + "_enable";
                if ( service.Name.indexOf("$") == 0 )
                    service.Name = service.Directory;
               if(!name.isEmpty() ){ return service; } //found the requested service - return it
                services << service;
                //qDebug() << "Added Service:" << cDir << service.Directory << service.Name << service.Tag;
            }
        }
    }
  return Service();
}
