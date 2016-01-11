//===========================================
//  PC-BSD source code
//  Copyright (c) 2015, PC-BSD Software/iXsystems
//  Available under the 3-clause BSD license
//  See the LICENSE file for full details

#include "sysadm-firewall.h"
#include <QtCore>
using namespace sysadm;
PortInfo Firewall::LookUpPort(int portNumber, QString portType)
{
    //Make sure that the port is valid
    if (portNumber < 0 || portNumber > 65535)
    {
        PortInfo returnValue;
        returnValue.Port = -1;
        returnValue.Description = "Port out of bounds";
        return returnValue;
    }

    //Check to see if things have been initialized
    if(portStrings == NULL)
        readServicesFile();


    PortInfo returnValue;
    //the port number is valid so set it
    returnValue.Port = portNumber;

    //make sure that the portType is cased in lower to match the service file and
    //then store it in the returnValue, since there isn't a huge point in checking
    //the validitiy of the type since /etc/services lists more than udp/tcp
    portType = portType.toLower();
    returnValue.PortType =  portType;

    //Check to see if it's a recommended port
    returnValue.Recommended = false;
    for(int i = 0; i < recommendedPortsSize; i++)
    {
        if (portNumber == recommendedPorts[i])
        {
            returnValue.Recommended = true;
        }
    }

   //Check to see if the port number is listed. The format in the file
   // is portname/portType. ex.: 22/tcp

   QStringList port = portStrings->filter(QString::number(portNumber) + "/" + portType);
   if(port.size() > 0)
   {
       //grab the first one, there may be duplicates due to colliding ports in the /etc/services file
       //but those are listed after the declaration for what the port officially should be used for
       QString line = port.at(0);

       //Split across spaces since it's whitespace delimited
       QStringList lineList = line.split(' ');

       //the keyword associated with the port is the first element in a line
       returnValue.Keyword = lineList.at(0);

       //if the size of the list is less than 3 then there is no description
       if(lineList.size() > 2)
       {
           QString description = lineList.at(2);
           //String the description back together from the end of the list
           for(int i = 3; i < lineList.size(); i++)
           {
               description += " " + lineList.at(i);
           }
           returnValue.Description = description;
       }
   }
    
   return returnValue;

}

void Firewall::OpenPort(int port, QString type)
{
    openports << QString::number(port)+"::::"+type;
    SaveOpenPorts();
}

void Firewall::ClosePort(int port, QString type)
{
    openports.removeAll( QString::number(port)+"::::"+type);
    SaveOpenPorts();
}

QVector<PortInfo> Firewall::OpenPorts()
{
    QVector<PortInfo> returnValue = QVector<PortInfo>(openports.length());

    for(int i=0; i<openports.length(); i++){
      returnValue.append( LookUpPort(openports[i].section("::::",0,0).toInt(),openports[i].section("::::",1,1)));
    }
    return returnValue;

}

bool Firewall::IsRunning()
{
    QProcess proc;
    proc.start("sysctl net.inet.ip.fw.enable");
    if(proc.waitForFinished() || proc.canReadLine())
    {
        if (proc.canReadLine())
        {
            QString line = proc.readLine();
            if(line.section(":",1,1).simplified().toInt() ==1) { return true; }
        }
    }
    return false;
}

void Firewall::Start()
{
    system("/etc/rc.d/ipfw start");
}

void Firewall::Stop()
{
    system("/etc/rc.d/ipfw stop");
}

void Firewall::Restart()
{
    system("/etc/rc.d/ipfw restart");
}

void Firewall::RestoreDefaults()
{
    //move the files out of the way
    system("mv /etc/ipfw.rules /etc/ipfw.rules.previous");
    system("mv /etc/ipfw.openports /etc/ipfw.openports.previous");
    //refresh/restart the rules files
    system("sh /usr/local/share/pcbsd/scripts/reset-firewall");
}

Firewall::Firewall()
{    
    readServicesFile();
    LoadOpenPorts();
}

Firewall::~Firewall()
{
    delete portStrings;
}

void Firewall::readServicesFile()
{
    portStrings = new QStringList();

    // /etc/services contains a file that lists the various port numbers
    // and their descriptions
    QFile* services = new QFile("/etc/services");
    services->open(QFile::ReadOnly);
    while(!services->atEnd())
    {
        QString line = services->readLine();
        //jump down past the comments
        if(line[0] == '#')
            continue;

        //remove all of the extraneous whitespace in the line
        line = line.simplified();

        portStrings->append(line);
    }
    services->close();
    delete services;
}

void Firewall::LoadOpenPorts()
{
  openports.clear();
  QFile file("/etc/ipfw.openports");
  if( file.open(QIODevice::ReadOnly) ){
    QTextStream in(&file);
    while( !in.atEnd() ){
      QString line = in.readLine();
      if(line.startsWith("#") || line.simplified().isEmpty()){ continue; }
      //File format: "<type> <port>" (nice and simple)
      openports << line.section(" ",1,1)+"::::"+line.section(" ",0,0);
    }
    file.close();
  }
  openports.sort(); //order them in ascending port order
}

void Firewall::SaveOpenPorts()
{
    //Convert to file format
      openports.sort(); //make sure they are still sorted by port
      QStringList fileout;
      for(int i=0; i<openports.length(); i++){
        fileout << openports[i].section("::::",1,1)+" "+openports[i].section("::::",0,0);
      }
      //Always make sure that the file always ends with a newline
      if(!fileout.isEmpty()){ fileout << ""; }
      //Save to file
      QFile file("/etc/ipfw.openports");
      if( file.open(QIODevice::WriteOnly | QIODevice::Truncate) ){
        QTextStream out(&file);
        out << fileout.join("\n");
        file.close();
      }
      //Re-load/start rules (just in case - it is a smart script)
      if(IsRunning()){ system("sh /usr/local/share/pcbsd/scripts/reset-firewall"); }
}
 
