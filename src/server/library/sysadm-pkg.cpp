//===========================================
//  PC-BSD source code
//  Copyright (c) 2015, PC-BSD Software/iXsystems
//  Available under the 3-clause BSD license
//  See the LICENSE file for full details
//===========================================
#include "sysadm-general.h"
#include "sysadm-pkg.h"
#include "sysadm-global.h"
#include "globals.h"

using namespace sysadm;

// ==================
//  INLINE FUNCTIONS
// ==================
inline QStringList ids_from_origins(QStringList origins, QSqlDatabase DB){
  QSqlQuery q("SELECT id FROM packages WHERE origin IN ('"+origins.join("', '")+"')",DB);
  QStringList out;
  while(q.next()){ out << q.value("id").toString(); }
  return out;
}

inline QStringList ids_from_names(QStringList names, QSqlDatabase DB){
  QSqlQuery q("SELECT id FROM packages WHERE name IN ('"+names.join("', '")+"')",DB);
  QStringList out;
  while(q.next()){ out << q.value("id").toString(); }
  return out;
}

//Get annotation variable/values
inline void annotations_from_ids(QStringList var_ids, QStringList val_ids, QJsonObject *out, QSqlDatabase DB){
  //Note: Both input lists *must* be the same length (one variable for one value)
  QStringList tot; tot << var_ids << val_ids;
  tot.removeDuplicates();
  int index = -1;
  QSqlQuery q("SELECT annotation, annotation_id FROM annotation WHERE annotation_id IN ('"+tot.join("', '")+"')",DB);
    while(q.next()){
	//qDebug() << "Got query result:" << q.value("annotation_id").toString() << q.value("annotation").toString();
	index = var_ids.indexOf(q.value("annotation_id").toString());
	while(index>=0){
	  var_ids.replace(index, q.value("annotation").toString());
	  index = var_ids.indexOf(q.value("annotation_id").toString());
	}
	index = val_ids.indexOf(q.value("annotation_id").toString());
	while(index>=0){
	  val_ids.replace(index, q.value("annotation").toString());
	  index = val_ids.indexOf(q.value("annotation_id").toString());
	}
    }
  //Now go through and add them to the JsonObject in pairs
  for(int i=0; i<var_ids.length(); i++){
    //qDebug() << "Got Annotation:" << var_ids[i] <<":"<<val_ids[i];
    out->insert(var_ids[i], val_ids[i]);
  }
}
//Get origin from package_id (for reverse lookups)
inline QStringList origins_from_package_ids(QStringList ids, QSqlDatabase DB){
  QSqlQuery q("SELECT origin FROM packages WHERE id IN ('"+ids.join("', '")+"')",DB);
  QStringList out;
  while(q.next()){ out << q.value("origin").toString(); }
  return out;
}
//Generic ID's -> Names function (known databases: users, groups, licenses, shlibs, categories, packages )
inline QStringList names_from_ids(QStringList ids, QString db, QSqlDatabase DB){
  QSqlQuery q("SELECT name FROM "+db+" WHERE id IN ('"+ids.join("', '")+"')",DB);
  QStringList out;
  while(q.next()){ out << q.value("name").toString(); }
  return out;
}
//provide values from ID's
inline QStringList provides_from_ids(QStringList ids, QSqlDatabase DB){
  QSqlQuery q("SELECT provide FROM provides WHERE id IN ('"+ids.join("', '")+"')",DB);
  QStringList out;
  while(q.next()){ out << q.value("provide").toString(); }
  return out;
}
//require values from ID's
inline QStringList requires_from_ids(QStringList ids, QSqlDatabase DB){
  QSqlQuery q("SELECT require FROM requires WHERE id IN ('"+ids.join("', '")+"')", DB);
  QStringList out;
  while(q.next()){ out << q.value("require").toString(); }
  return out;
}

//conflict ID's from package ID's
inline QStringList conflicts_from_ids(QStringList ids, QSqlDatabase DB){
  QSqlQuery q("SELECT conflict_id FROM pkg_conflicts WHERE package_id IN ('"+ids.join("', '")+"')", DB);
  QStringList out;
  while(q.next()){ out << q.value("conflict_id").toString(); }
  qDebug() << "Last Conflict detection Error:" << q.lastError().text();
  return out;
}

//dependencies from package ID's
inline QStringList depends_from_ids(QStringList ids, QSqlDatabase DB){
  //Note: This returns package names, not ID's
  QSqlQuery q("SELECT name FROM deps WHERE package_id IN ('"+ids.join("', '")+"')", DB);
  QStringList out;
  while(q.next()){ out << q.value("name").toString(); }
  return out;
}


inline QString getRepoFile(QString repo){
  if(repo=="local"){  return "/var/db/pkg/local.sqlite"; }
  else{ return ("/var/db/pkg/repo-"+repo+".sqlite"); }
}
inline QString openDB(QString repo){
  //This ensures that each request for a database gets its own unique connection
  //  (preventing conflict between concurrent calls)
    QSqlDatabase DB = QSqlDatabase::addDatabase("QSQLITE", repo+QUuid::createUuid().toString());
    DB.setConnectOptions("QSQLITE_OPEN_READONLY=1");
    DB.setHostName("localhost");
    QString path = getRepoFile(repo);
    DB.setDatabaseName(path); //path to the database file
    //qDebug() << "New DB:" << repo << DB.connectionName();
  return DB.connectionName();
}

/*inline QSqlDatabase closeDB(QSqlDatabase *DB){
  qDebug() << " - Close DB:" << DB->connectionName();
  //completely close/remove the database connection
  QString conn = DB->connectionName();
  DB->close();
  QSqlDatabase::removeDatabase(conn);
  qDebug() << " - done closing";
}*/

// =================
//  MAIN FUNCTIONS
// =================
QJsonObject PKG::pkg_info(QStringList origins, QString repo, QString category, bool fullresults){
  QJsonObject retObj;
  //if(origins.contains("math/R")){ qDebug() << "pkg_info:" << repo << category; }
  QString dbconn = openDB(repo);
  if(!dbconn.isEmpty()){
  QSqlDatabase DB = QSqlDatabase::database(dbconn);
  if(!DB.isOpen()){ return retObj; } //could not open DB (file missing?)
  //Now do all the pkg info, one pkg origin at a time
  origins.removeAll("");
  origins.removeDuplicates();
    QString q_string = "SELECT * FROM packages";
    if(!origins.isEmpty()){
      q_string.append(" WHERE name IN ('"+origins.join("', '")+"')");
      //Also keep the ordering of the origins preserved
      /*q_string.append(" ORDER BY CASE origins ");
      for(int i=0; i<origins.length(); i++){ q_string.append("WHEN '"+origins[i]+"' THEN '"+QString::number(i+1)+"' "); }
      q_string.append("END");*/
    }
    else if(!category.isEmpty()){ q_string.append(" WHERE origin LIKE '"+category+"/%'"); }
    //if(origins.contains("math/R")){ qDebug() << "Query:" << q_string; }
  QSqlQuery query(q_string, DB);
    while(query.next()){
	QString id = query.value("id").toString(); //need this pkg id for later
	QString name = query.value("name").toString(); //need the origin for later
	//if(origins.contains("math/R")){ qDebug() << "Found origin:" << origin << id; }
      if(id.isEmpty() || name.isEmpty()){ continue; }
      QJsonObject info;
      //General info
      for(int i=0; i<query.record().count(); i++){
        info.insert(query.record().fieldName(i), query.value(i).toString() );
      }
      //ANNOTATIONS
      QSqlQuery q2("SELECT tag_id, value_id FROM pkg_annotation WHERE package_id = '"+id+"'", DB);
      QStringList tags, vals; //both the value and the variable are id tags to entries in the annotations table.
      while(q2.next()){
	  tags << q2.value("tag_id").toString(); vals << q2.value("value_id").toString();
      }
      if(!tags.isEmpty()){ annotations_from_ids(tags, vals, &info, DB); }
      if(!fullresults){ retObj.insert(name,info); continue; } //skip the rest of the info queries
      //OPTIONS
      QSqlQuery q3("SELECT value, option FROM pkg_option INNER JOIN option ON pkg_option.option_id = option.option_id WHERE pkg_option.package_id = '"+id+"'", DB);
      QJsonObject options;
      while(q3.next()){
	//for(int r=0; r<q3.record().count(); r++){ qDebug() << "Field:" << q3.record().fieldName(r); qDebug() << "Value:" << q3.record().value(r).toString(); }
	options.insert(q3.value("option").toString(), q3.value("value").toString());
      }
      if(!options.isEmpty()){ info.insert("options",options); }
      //DEPENDENCIES
      QSqlQuery q4("SELECT origin, name FROM deps WHERE package_id = '"+id+"'", DB);
      QStringList tmpList, tmpListN;
       while(q4.next()){
	  tmpList << q4.value("origin").toString();
           tmpListN << q4.value("name").toString();
      } //end deps query
      if(!tmpList.isEmpty()){
        //info.insert("dependencies_origins", QJsonArray::fromStringList(tmpList) );
        info.insert("dependencies", QJsonArray::fromStringList(tmpListN) );
      }
      //FILES
      QSqlQuery q5("SELECT path FROM files WHERE package_id = '"+id+"'", DB);
      tmpList.clear();
       while(q5.next()){  tmpList << q5.value("path").toString(); }
      if(!tmpList.isEmpty()){ info.insert("files", QJsonArray::fromStringList(tmpList) ); }
      //REVERSE DEPENDENCIES
      QSqlQuery q6("SELECT package_id FROM deps WHERE name = '"+name+"'", DB);
      tmpList.clear();
       while(q6.next()){ tmpList << q6.value("package_id").toString(); }
       if(!tmpList.isEmpty()){
         //info.insert("reverse_dependencies_origins", QJsonArray::fromStringList(origins_from_package_ids(tmpList, DB)) );
         info.insert("reverse_dependencies", QJsonArray::fromStringList(names_from_ids(tmpList, "packages", DB)) );
       }
       //USERS
      QSqlQuery q7("SELECT user_id FROM pkg_users WHERE package_id = '"+id+"'", DB);
      tmpList.clear();
       while(q7.next()){ tmpList << q7.value("user_id").toString(); }
       if(!tmpList.isEmpty()){ info.insert("users", QJsonArray::fromStringList(names_from_ids(tmpList, "users", DB)) ); }
       //GROUPS
      QSqlQuery q8("SELECT group_id FROM pkg_groups WHERE package_id = '"+id+"'", DB);
      tmpList.clear();
       while(q8.next()){ tmpList << q8.value("group_id").toString(); }
       if(!tmpList.isEmpty()){ info.insert("groups", QJsonArray::fromStringList(names_from_ids(tmpList, "users", DB)) ); }
       //LICENSES
      QSqlQuery q9("SELECT license_id FROM pkg_licenses WHERE package_id = '"+id+"'", DB);
      tmpList.clear();
       while(q9.next()){ tmpList << q9.value("license_id").toString(); }
       if(!tmpList.isEmpty()){ info.insert("licenses", QJsonArray::fromStringList(names_from_ids(tmpList, "licenses", DB)) ); }
       //SHARED LIBS (REQUIRED)
      QSqlQuery q10("SELECT shlib_id FROM pkg_shlibs_required WHERE package_id = '"+id+"'", DB);
      tmpList.clear();
       while(q10.next()){ tmpList << q10.value("shlib_id").toString(); }
       if(!tmpList.isEmpty()){ info.insert("shlibs_required", QJsonArray::fromStringList(names_from_ids(tmpList, "shlibs", DB)) ); }
       //SHARED LIBS (PROVIDED)
      QSqlQuery q11("SELECT shlib_id FROM pkg_shlibs_provided WHERE package_id = '"+id+"'", DB);
      tmpList.clear();
       while(q11.next()){ tmpList << q11.value("shlib_id").toString(); }
       if(!tmpList.isEmpty()){ info.insert("shlibs_provided", QJsonArray::fromStringList(names_from_ids(tmpList, "shlibs", DB)) ); }
       //CONFLICTS
      QSqlQuery q12("SELECT conflict_id FROM pkg_conflicts WHERE package_id = '"+id+"'", DB);
      tmpList.clear();
       while(q12.next()){ tmpList << q12.value("conflict_id").toString(); }
       if(!tmpList.isEmpty()){ info.insert("conflicts", QJsonArray::fromStringList(origins_from_package_ids(tmpList, DB)) ); }
      //CONFIG FILES
      QSqlQuery q13("SELECT path FROM config_files WHERE package_id = '"+id+"'", DB);
      tmpList.clear();
       while(q13.next()){  tmpList << q13.value("path").toString(); }
      if(!tmpList.isEmpty()){ info.insert("config_files", QJsonArray::fromStringList(tmpList) ); }
      //PROVIDES
      QSqlQuery q14("SELECT provide_id FROM pkg_provides WHERE package_id = '"+id+"'", DB);
      tmpList.clear();
       while(q14.next()){ tmpList << q14.value("provide_id").toString(); }
       if(!tmpList.isEmpty()){ info.insert("provides", QJsonArray::fromStringList(provides_from_ids(tmpList, DB)) ); }
      //REQUIRES
      QSqlQuery q15("SELECT require_id FROM pkg_requires WHERE package_id = '"+id+"'", DB);
      tmpList.clear();
       while(q15.next()){ tmpList << q15.value("require_id").toString(); }
       if(!tmpList.isEmpty()){ info.insert("requires", QJsonArray::fromStringList(requires_from_ids(tmpList, DB)) ); }\
       //Now insert this information into the main object
       retObj.insert(name,info);
  } //end loop over pkg matches
  DB.close();
  }//end if dbconn exists (force DB out of scope now)
  //closeDB(&DB);
  QSqlDatabase::removeDatabase(dbconn);
  return retObj;
}

QStringList PKG::pkg_search(QString repo, QString searchterm, QStringList searchexcludes, QString category){
  QString dbconn = openDB(repo);
  QStringList found;
  if(!dbconn.isEmpty()){
  QSqlDatabase DB = QSqlDatabase::database(dbconn);
  if(!DB.isOpen()){ return QStringList(); } //could not open DB (file missing?)

  QStringList terms = searchterm.split(" ",QString::SkipEmptyParts);
  searchexcludes.removeAll("");
  QString q_string;
int numtry = 0;
while(found.isEmpty() && numtry<2){
  if(numtry<1 && !searchterm.contains(" ")){ //single-word-search (exact names never have multiple words)
    q_string = "SELECT name FROM packages WHERE name = '"+searchterm+"' OR origin LIKE '%/"+searchterm+"'";
    if(!category.isEmpty()){ q_string.append(" AND origin LIKE '"+category+"/%'"); }
    if(!searchexcludes.isEmpty()){ q_string.append(" AND name NOT LIKE '%"+searchexcludes.join("%' AND name NOT LIKE '%")+"%'"); }
    q_string.append(" COLLATE NOCASE"); // Case insensitive
    QSqlQuery query(q_string, DB);
    while(query.next()){
	found << query.value("name").toString(); //need the origin for later
    }
  }
  if(found.length()<60 && numtry<1){
    //Expand the search to names containing the term
    q_string = "SELECT name FROM packages WHERE name LIKE '"+searchterm+"%'";
    if(!category.isEmpty()){ q_string.append(" AND origin LIKE '"+category+"/%'"); }
    if(!searchexcludes.isEmpty()){ q_string.append(" AND name NOT LIKE '%"+searchexcludes.join("%' AND name NOT LIKE '%")+"%'"); }
    QSqlQuery q2(q_string, DB);
    while(q2.next()){
	found << q2.value("name").toString(); //need the origin for later
    }
  }
  if(found.length()<60 && numtry<1){
    //Expand the search to names containing the term
    q_string = "SELECT name FROM packages WHERE name LIKE '%"+searchterm+"%'";
    if(!category.isEmpty()){ q_string.append(" AND origin LIKE '"+category+"/%'"); }
    if(!searchexcludes.isEmpty()){ q_string.append(" AND name NOT LIKE '%"+searchexcludes.join("%' AND name NOT LIKE '%")+"%'"); }
    QSqlQuery q2(q_string, DB);
    while(q2.next()){
	found << q2.value("name").toString(); //need the origin for later
    }
  }
  if(found.length()<60){
    //Expand the search to comments
    if(terms.length()<2){ q_string = "SELECT nameFROM packages WHERE comment LIKE '%"+searchterm+"%'"; }
    else if(numtry==0){ q_string = "SELECT name FROM packages WHERE comment LIKE '%"+terms.join("%' AND comment LIKE '%")+"%'"; }
    else if(numtry==1){ q_string = "SELECT name FROM packages WHERE comment LIKE '%"+terms.join("%' OR comment LIKE '%")+"%'"; }
    if(!category.isEmpty()){ q_string.append(" AND origin LIKE '"+category+"/%'"); }
    if(!searchexcludes.isEmpty()){ q_string.append(" AND comment NOT LIKE '%"+searchexcludes.join("%' AND comment NOT LIKE '%")+"%'"); }
    QSqlQuery q2(q_string, DB);
    while(q2.next()){
	found << q2.value("name").toString(); //need the origin for later
    }
  }
  if(found.length()<100){
    //Expand the search to full descriptions
    if(terms.length()<2){ q_string = "SELECT name FROM packages WHERE desc LIKE '%"+searchterm+"%'"; }
    else if(numtry==0){ q_string = "SELECT name FROM packages WHERE desc LIKE '%"+terms.join("%' AND desc LIKE '%")+"%'"; }
    else if(numtry==1){ q_string = "SELECT name FROM packages WHERE desc LIKE '%"+terms.join("%' OR desc LIKE '%")+"%'"; }
    if(!category.isEmpty()){ q_string.append(" AND origin LIKE '"+category+"/%'"); }
    if(!searchexcludes.isEmpty()){ q_string.append(" AND desc NOT LIKE '%"+searchexcludes.join("%' AND desc NOT LIKE '%")+"%'"); }
    QSqlQuery q2(q_string, DB);
    while(q2.next()){
	found << q2.value("name").toString(); //need the origin for later
    }
  }
  //Now bump the try count
  numtry++;
} //end while loop  for number of tries
  //if(searchterm=="R"){ qDebug()<< "Search:" << searchterm << category << found; }
  //disable open queries before closing DB
  DB.close();
  //closeDB(&DB);
 }//end if dbconn exists (force DB out of scope now)
  QSqlDatabase::removeDatabase(dbconn);
  found.removeDuplicates();
  return found;
}

QJsonArray PKG::list_categories(QString repo){
  QString dbconn = openDB(repo);
  QStringList found;
  if(!dbconn.isEmpty()){
  QSqlDatabase DB = QSqlDatabase::database(dbconn);
  if(!DB.isOpen()){ return QJsonArray(); } //could not open DB (file missing?)

  //Get all the pkg origins for this repo
  QStringList origins;
    QSqlQuery q_o("SELECT origin FROM packages", DB);
    while(q_o.next()){
	origins << q_o.value("origin").toString(); //need the origin for later
    }
  //Now get all the categories
  QString q_string = "SELECT name FROM categories";
  QSqlQuery query(q_string, DB);
  while(query.next()){
    found << query.value("name").toString(); //need the origin for later
  }

  //Now check all the categories to ensure that pkgs exist within it
    for(int i=0; i<found.length(); i++){
      if(origins.filter(found[i]+"/").isEmpty()){ found.removeAt(i); i--; }
    }
  //Cleanup and return
  //q_o.clear(); query.clear(); //disable open queries before closing DB
  DB.close();
  } //force DB out of scope
  //closeDB(&DB);
  QSqlDatabase::removeDatabase(dbconn);
  if(!found.isEmpty()){ return QJsonArray::fromStringList(found); }
  else{ return QJsonArray(); }
}

QJsonArray PKG::list_repos(bool updated){
  QString dbdir = "/var/db/pkg/repo-%1.sqlite";
  QStringList repodirs; repodirs << "/etc/pkg" << "/etc/pkg/repos" << "/usr/local/etc/pkg" << "/usr/local/etc/pkg/repos";
  QStringList found;
  found << "local"; //There is always a local database (for installed pkgs)
  for(int d=0; d<repodirs.length(); d++){
    if(!QFile::exists(repodirs[d])){ continue; }
    QDir confdir(repodirs[d]);
    QStringList confs = confdir.entryList(QStringList() << "*.conf", QDir::Files);
    for(int i=0; i<confs.length(); i++){
      QStringList contents = General::readTextFile(confdir.absoluteFilePath(confs[i]));
      //Scan for any comments on the top of the file and remove them
      for(int l=0; l<contents.length(); l++){
        if(contents[l].isEmpty() || contents[l].startsWith("#")){ contents.removeAt(l); l--; }
        else{ break; }
      }
      QStringList repoinfo = contents.join("\n").split("\n}");
      for(int j=0; j<repoinfo.length(); j++){
        qDebug() << "Repoinfo:" << repoinfo[j];
        QString repo = repoinfo[j].section(":",0,0).simplified();
        QString enabled = repoinfo[j].section("enabled:",1,-1).section(":",0,0).toLower();
        bool isEnabled = true;
        if(enabled.contains("no") || enabled.contains("false")){ isEnabled = false; }
       qDebug() << "Checking Repo:" << repo << enabled << isEnabled;
        if(QFile::exists(dbdir.arg(repo)) && isEnabled){ found << repo; }
      } //loop over repos listed in conf
    } //loop over confs in repodir
  } //loop over repodirs
  if(found.length()<2 && !updated){
    //Only the local repo could be found - update the package repos and try again
    DProcess* proc = DISPATCHER->queueProcess(Dispatcher::PKG_QUEUE, "internal_sysadm_pkg_repo_update_sync", "pkg update");
    proc->waitForFinished();
    return list_repos(true); //try again recursively (will not try to update again)
  }
  return QJsonArray::fromStringList(found);
}

QJsonObject PKG::evaluateInstall(QStringList origins, QString repo){
  //qDebug() << "Verify Install:" << origins << repo;
  QJsonObject out;
    out.insert("install_origins", QJsonArray::fromStringList(origins) );
    out.insert("repo", repo);
  if(repo=="local" || origins.isEmpty()){ return out; } //nothing to do
  QString dbconn = openDB(repo);
  QString ldbconn = openDB("local");
  if(!dbconn.isEmpty()){
    QSqlDatabase DB = QSqlDatabase::database(dbconn);
    QSqlDatabase LDB = QSqlDatabase::database(ldbconn);
    if(!DB.isOpen() || !LDB.isOpen()){ return out; } //could not open DB (file missing?)

    //First get the list of all packages which need to be installed (ID's) from the remote database
    QStringList toInstall_id;
    QStringList tmp;
    if(origins.first().contains("/")){ tmp = names_from_ids( ids_from_origins(origins, DB), "packages", DB); }
    else{ tmp = origins; } //already given names
    //qDebug() << " - Initial names:" << tmp;
    while(!tmp.isEmpty()){
      QStringList ids = ids_from_names(tmp, DB);
      for(int i=0; i<ids.length(); i++){
        if(toInstall_id.contains(ids[i])){ ids.removeAt(i); i--; } //remove any duplicate/evaluated ID's
      }
      if(ids.isEmpty()){ break; } //stop the loop - found the last round of dependencies
      toInstall_id << ids; //add these to the list which are going to get installed
      tmp = depends_from_ids(ids, DB); //now get the depdendencies of these packages
      //qDebug() << " - Iteration names:" << tmp;
    }

    //Now go through and remove any packages from the list which are already installed locally
    QStringList names = names_from_ids(toInstall_id, "packages", DB); //same order
    //qDebug() << " - Total Names:" << names;
    QStringList local_names = names_from_ids( ids_from_names(names, LDB), "packages", LDB);
    //qDebug() << " - Local Names:" << local_names;
    for(int i=0; i<local_names.length(); i++){
      names.removeAll(local_names[i]);
    }
    //qDebug() << " - Filtered Names:" << names;
    toInstall_id = ids_from_names(names, DB); //now get the shorter/filtered list of ID's (remote)
    //qDebug() << " - Filtered ID's:" << toInstall_id;
    //Get the list of conflicting packages which are already installed
    QStringList conflict_ids = conflicts_from_ids(toInstall_id, DB); //also get the list of any conflicts for these packages
      conflict_ids.removeDuplicates();
    QStringList conflict_names = names_from_ids(conflict_ids, "packages", DB);
    //qDebug() << " - Conflicts (remote):" << conflict_ids << conflict_names;
    out.insert("conflicts", QJsonArray::fromStringList(names_from_ids( ids_from_names(conflict_names, LDB), "packages", LDB) ) );
    //Now assemble all the information about the packages (remote database)
    QJsonObject install;
    //qDebug() << "Perform Query";
    QSqlQuery qi("SELECT * FROM packages WHERE id IN ('"+toInstall_id.join("', '")+"')", DB);
    while(qi.next()){
      QJsonObject obj;
      obj.insert( "name", qi.value("name").toString());
      obj.insert( "origin", qi.value("origin").toString());
      obj.insert( "pkgsize", qi.value("pkgsize").toString());
      obj.insert( "flatsize", qi.value("flatsize").toString());
      obj.insert( "version", qi.value("version").toString());
      obj.insert( "comment", qi.value("comment").toString());
      install.insert(qi.value("name").toString(), obj);
    }
    //qDebug() << "Final Install Object:" << install;
    //qDebug() << "Last Query Error:" << qi.lastError().text();
    //Add the info to the output object and close the databases
    out.insert("install", install);
    DB.close();
    LDB.close();
  } //force DB out of scope
  QSqlDatabase::removeDatabase(dbconn);
  QSqlDatabase::removeDatabase(ldbconn);
  return out;
}

//=================
//pkg modification routines (dispatcher events for notifications)
//=================
QJsonObject PKG::pkg_install(QStringList origins, QString repo){
  //Generate the command to run
  QString cmd = "pkg install -y %1";
  if(!repo.isEmpty() && repo!="local"){ cmd = cmd.arg("--repository \""+repo+"\" %1"); }
  cmd = cmd.arg( origins.join(" ") );
  //Now kick off the dispatcher process (within the pkg queue - since only one pkg process can run at a time)
  QString ID = "sysadm_pkg_install-"+QUuid::createUuid().toString(); //create a random tag for the process
  DISPATCHER->queueProcess(Dispatcher::PKG_QUEUE, ID, cmd);
  //Now return the info about the process
  QJsonObject obj;
    obj.insert("status", "pending");
    obj.insert("proc_cmd",cmd);
    obj.insert("proc_id",ID);
  return obj;
}

QJsonObject PKG::pkg_remove(QStringList origins, bool recursive){
  //Generate the command to run
  QString cmd = "pkg delete -y %1";
  if(recursive){ cmd = cmd.arg("-R %1"); } //also remove all packages which depend on these pkgs
  cmd = cmd.arg( origins.join(" ") );
  //Now kick off the dispatcher process (within the pkg queue - since only one pkg process can run at a time)
  QString ID = "sysadm_pkg_remove-"+QUuid::createUuid().toString(); //create a random tag for the process
  DISPATCHER->queueProcess(Dispatcher::PKG_QUEUE, ID, cmd);
  //Now return the info about the process
  QJsonObject obj;
    obj.insert("status", "pending");
    obj.insert("proc_cmd",cmd);
    obj.insert("proc_id",ID);
  return obj;
}

QJsonObject PKG::pkg_lock(QStringList origins){
  //Generate the command to run
  QString cmd = "pkg lock -y %1";
  cmd = cmd.arg( origins.join(" ") );
  //Now kick off the dispatcher process (within the pkg queue - since only one pkg process can run at a time)
  QString ID = "sysadm_pkg_lock-"+QUuid::createUuid().toString(); //create a random tag for the process
  DISPATCHER->queueProcess(Dispatcher::PKG_QUEUE, ID, cmd);
  //Now return the info about the process
  QJsonObject obj;
    obj.insert("status", "pending");
    obj.insert("proc_cmd",cmd);
    obj.insert("proc_id",ID);
  return obj;
}

QJsonObject PKG::pkg_unlock(QStringList origins){
  //Generate the command to run
  QString cmd = "pkg unlock -y %1";
  cmd = cmd.arg( origins.join(" ") );
  //Now kick off the dispatcher process (within the pkg queue - since only one pkg process can run at a time)
  QString ID = "sysadm_pkg_unlock-"+QUuid::createUuid().toString(); //create a random tag for the process
  DISPATCHER->queueProcess(Dispatcher::PKG_QUEUE, ID, cmd);
  //Now return the info about the process
  QJsonObject obj;
    obj.insert("status", "pending");
    obj.insert("proc_cmd",cmd);
    obj.insert("proc_id",ID);
  return obj;
}

//==================
//pkg administration routines
//==================
QJsonObject PKG::pkg_update(bool force){
  //Generate the command to run
  QString cmd = "pkg update";
  if(force){ cmd.append(" -f"); }
  //Now kick off the dispatcher process (within the pkg queue - since only one pkg process can run at a time)
  QString ID = "sysadm_pkg_update-"+QUuid::createUuid().toString(); //create a random tag for the process
  DISPATCHER->queueProcess(Dispatcher::PKG_QUEUE, ID, cmd);
  //Now return the info about the process
  QJsonObject obj;
    obj.insert("status", "pending");
    obj.insert("proc_cmd",cmd);
    obj.insert("proc_id",ID);
  return obj;
}

QJsonObject PKG::pkg_check_upgrade(){
  //Generate the command to run
  QString cmd = "pkg upgrade -n";
  //Now kick off the dispatcher process (within the pkg queue - since only one pkg process can run at a time)
  QString ID = "sysadm_pkg_check_upgrade-"+QUuid::createUuid().toString(); //create a random tag for the process
  DISPATCHER->queueProcess(Dispatcher::PKG_QUEUE, ID, cmd);
  //Now return the info about the process
  QJsonObject obj;
    obj.insert("status", "pending");
    obj.insert("proc_cmd",cmd);
    obj.insert("proc_id",ID);
  return obj;
}

QJsonObject PKG::pkg_upgrade(){
  //Generate the command to run
  QString cmd = "pkg upgrade -y";
  //Now kick off the dispatcher process (within the pkg queue - since only one pkg process can run at a time)
  QString ID = "sysadm_pkg_upgrade-"+QUuid::createUuid().toString(); //create a random tag for the process
  DISPATCHER->queueProcess(Dispatcher::PKG_QUEUE, ID, cmd);
  //Now return the info about the process
  QJsonObject obj;
    obj.insert("status", "pending");
    obj.insert("proc_cmd",cmd);
    obj.insert("proc_id",ID);
  return obj;
}

QJsonObject PKG::pkg_audit(){
  //Generate the command to run
  QString cmd = "pkg audit -qr";
  //Now kick off the dispatcher process (within the pkg queue - since only one pkg process can run at a time)
  QString ID = "sysadm_pkg_audit-"+QUuid::createUuid().toString(); //create a random tag for the process
  DISPATCHER->queueProcess(Dispatcher::PKG_QUEUE, ID, cmd);
  //Now return the info about the process
  QJsonObject obj;
    obj.insert("status", "pending");
    obj.insert("proc_cmd",cmd);
    obj.insert("proc_id",ID);
  return obj;
}

QJsonObject PKG::pkg_autoremove(){
  //Generate the command to run
  QString cmd = "pkg autoremove -y";
  //Now kick off the dispatcher process (within the pkg queue - since only one pkg process can run at a time)
  QString ID = "sysadm_pkg_autoremove-"+QUuid::createUuid().toString(); //create a random tag for the process
  DISPATCHER->queueProcess(Dispatcher::PKG_QUEUE, ID, cmd);
  //Now return the info about the process
  QJsonObject obj;
    obj.insert("status", "pending");
    obj.insert("proc_cmd",cmd);
    obj.insert("proc_id",ID);
  return obj;
}
