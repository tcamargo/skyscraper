/***************************************************************************
 *            igdb.cpp
 *
 *  Sun Aug 26 12:00:00 CEST 2018
 *  Copyright 2018 Lars Muldjord
 *  muldjordlars@gmail.com
 ****************************************************************************/
/*
 *  This file is part of skyscraper.
 *
 *  skyscraper is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  skyscraper is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with skyscraper; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <QJsonArray>

#include "igdb.h"
#include "strtools.h"
#include "nametools.h"

Igdb::Igdb(Settings *config) : AbstractScraper(config)
{
  connect(&manager, &NetComm::dataReady, &q, &QEventLoop::quit);

  baseUrl = "https://api-v3.igdb.com";

  searchUrlPre = "https://api-v3.igdb.com";
  
  fetchOrder.append(RELEASEDATE);
  fetchOrder.append(RATING);
  fetchOrder.append(PUBLISHER);
  fetchOrder.append(DEVELOPER);
  fetchOrder.append(DESCRIPTION);
  fetchOrder.append(PLAYERS);
  fetchOrder.append(TAGS);
  fetchOrder.append(AGES);
}

void Igdb::getSearchResults(QList<GameEntry> &gameEntries,
				QString searchName, QString platform)
{
  // Request list of games but don't allow re-releases ("game.version_parent = null")
  manager.request(baseUrl + "/search/", "fields game.name,game.platforms.name; search \"" + searchName + "\"; where game != null & game.version_parent = null;", "user-key", StrTools::unMagic("136;213;169;133;171;147;206;117;211;152;214;221;209;213;157;197;136;158;212;220;171;211;160;215;202;172;216;125;172;174;151;171"));
  q.exec();
  data = manager.getData();

  if(data.contains("Limits exceeded")) {
    printf("\033[1;31mThe global monthly limit for the IGDB scraping module has been reached, can't continue...\033[0m\n");
    reqRemaining = 0;
  }

  jsonDoc = QJsonDocument::fromJson(data);
  if(jsonDoc.isEmpty()) {
    return;
  }

  if(jsonDoc.array().first().toObject().value("status").toInt() == 403) {
    printf("\033[1;31mThe global monthly limit for the IGDB scraping module has been reached, can't continue...\033[0m\n");
    reqRemaining = 0;
  }

  QJsonArray jsonGames = jsonDoc.array();

  foreach(const QJsonValue &jsonGame, jsonGames) {
    GameEntry game;
    
    game.title = jsonGame.toObject().value("game").toObject().value("name").toString();
    game.id = QString::number(jsonGame.toObject().value("game").toObject().value("id").toInt());

    QJsonArray jsonPlatforms = jsonGame.toObject().value("game").toObject().value("platforms").toArray();
    foreach(const QJsonValue &jsonPlatform, jsonPlatforms) {
      game.id.append(";" + QString::number(jsonPlatform.toObject().value("id").toInt()));
      game.platform = jsonPlatform.toObject().value("name").toString();
      if(platformMatch(game.platform, platform)) {
	gameEntries.append(game);
      }
    }
  }
}

void Igdb::getGameData(GameEntry &game)
{
  manager.request(baseUrl + "/games/", "fields age_ratings.rating,age_ratings.category,total_rating,cover.url,game_modes.slug,genres.name,screenshots.url,summary,release_dates.date,release_dates.region,release_dates.platform,involved_companies.company.name,involved_companies.developer,involved_companies.publisher; where id = " + game.id.split(";").first() + ";", "user-key", StrTools::unMagic("136;213;169;133;171;147;206;117;211;152;214;221;209;213;157;197;136;158;212;220;171;211;160;215;202;172;216;125;172;174;151;171"));
  q.exec();
  data = manager.getData();

  jsonDoc = QJsonDocument::fromJson(data);
  if(jsonDoc.isEmpty()) {
    return;
  }

  jsonObj = jsonDoc.array().first().toObject();
  
  for(int a = 0; a < fetchOrder.length(); ++a) {
    switch(fetchOrder.at(a)) {
    case DESCRIPTION:
      getDescription(game);
      break;
    case DEVELOPER:
      getDeveloper(game);
      break;
    case PUBLISHER:
      getPublisher(game);
      break;
    case PLAYERS:
      getPlayers(game);
      break;
    case RATING:
      getRating(game);
      break;
    case AGES:
      getAges(game);
      break;
    case TAGS:
      getTags(game);
      break;
    case RELEASEDATE:
      getReleaseDate(game);
      break;
    case COVER:
      if(config->cacheCovers) {
	getCover(game);
      }
      break;
    case SCREENSHOT:
      if(config->cacheScreenshots) {
	getScreenshot(game);
      }
      break;
    default:
      ;
    }
  }
}

void Igdb::getReleaseDate(GameEntry &game)
{
  QJsonArray jsonDates = jsonObj.value("release_dates").toArray();
  bool regionMatch = false;
  foreach(QString region, regionPrios) {
    foreach(const QJsonValue &jsonDate, jsonDates) {
      int regionEnum = jsonDate.toObject().value("region").toInt();
      QString curRegion = "";
      if(regionEnum == 1)
	curRegion = "eu";
      else if(regionEnum == 2)
	curRegion = "us";
      else if(regionEnum == 3)
	curRegion = "au";
      else if(regionEnum == 4)
	curRegion = "nz";
      else if(regionEnum == 5)
	curRegion = "jp";
      else if(regionEnum == 6)
	curRegion = "cn";
      else if(regionEnum == 7)
	curRegion = "asi";
      else if(regionEnum == 8)
	curRegion = "wor";
      if(QString::number(jsonDate.toObject().value("platform").toInt()) ==
	 game.id.split(";").last() &&
	 region == curRegion) {
	game.releaseDate = QDateTime::fromMSecsSinceEpoch((qint64)jsonDate.toObject().value("date").toInt() * 1000).toString("yyyyMMdd");
	regionMatch = true;
	break;
      }
    }
    if(regionMatch)
      break;
  }
}

void Igdb::getPlayers(GameEntry &game)
{
  // This is a bit of a hack. The unique identifiers are as follows:
  // 1 = Single Player
  // 2 = Multiplayer
  // 3 = Cooperative
  // 4 = Split screen
  // 5 = MMO
  // So basically if != 1 it's at least 2 players. That's all we can gather from this
  game.players = "1";
  QJsonArray jsonPlayers = jsonObj.value("game_modes").toArray();
  foreach(const QJsonValue &jsonPlayer, jsonPlayers) {
    if(jsonPlayer.toObject().value("id").toInt() != 1) {
      game.players = "2";
      break;
    }
  }
}

void Igdb::getTags(GameEntry &game)
{
  QJsonArray jsonGenres = jsonObj.value("genres").toArray();
  foreach(const QJsonValue &jsonGenre, jsonGenres) {
    game.tags.append(jsonGenre.toObject().value("name").toString() + ", ");
  }
  game.tags = game.tags.left(game.tags.length() - 2);
}

void Igdb::getAges(GameEntry &game)
{
  int agesEnum = jsonObj.value("age_ratings").toArray().first().toObject().value("rating").toInt();
  if(agesEnum == 1) {
    game.ages = "3";
  } else if(agesEnum == 2) {
    game.ages = "7";
  } else if(agesEnum == 3) {
    game.ages = "12";
  } else if(agesEnum == 4) {
    game.ages = "16";
  } else if(agesEnum == 5) {
    game.ages = "18";
  } else if(agesEnum == 6) {
    // Rating pending
  } else if(agesEnum == 7) {
    game.ages = "EC";
  } else if(agesEnum == 8) {
    game.ages = "E";
  } else if(agesEnum == 9) {
    game.ages = "E10";
  } else if(agesEnum == 10) {
    game.ages = "T";
  } else if(agesEnum == 11) {
    game.ages = "M";
  } else if(agesEnum == 12) {
    game.ages = "AO";
  }
}

void Igdb::getPublisher(GameEntry &game)
{
  QJsonArray jsonCompanies = jsonObj.value("involved_companies").toArray();
  foreach(const QJsonValue &jsonCompany, jsonCompanies) {
    if(jsonCompany.toObject().value("publisher").toBool() == true) {
      game.publisher = jsonCompany.toObject().value("company").toObject().value("name").toString();
      return;
    }
  }  
}

void Igdb::getDeveloper(GameEntry &game)
{
  QJsonArray jsonCompanies = jsonObj.value("involved_companies").toArray();
  foreach(const QJsonValue &jsonCompany, jsonCompanies) {
    if(jsonCompany.toObject().value("developer").toBool() == true) {
      game.developer = jsonCompany.toObject().value("company").toObject().value("name").toString();
      return;
    }
  }  
}

void Igdb::getDescription(GameEntry &game)
{
  QJsonValue jsonValue = jsonObj.value("summary");
  if(jsonValue != QJsonValue::Undefined) {
    game.description = StrTools::stripHtmlTags(jsonValue.toString());
  }
}

void Igdb::getRating(GameEntry &game)
{
  QJsonValue jsonValue = jsonObj.value("total_rating");
  if(jsonValue != QJsonValue::Undefined) {
    double rating = jsonValue.toDouble();
    if(rating != 0.0) {
      game.rating = QString::number(rating / 100.0);
    }
  }
}

QList<QString> Igdb::getSearchNames(const QFileInfo &info)
{
  QString baseName = info.completeBaseName();

  if(!config->aliasMap[baseName].isEmpty()) {
    baseName = config->aliasMap[baseName];
  } else if(info.suffix() == "lha") {
    QString nameWithSpaces = config->whdLoadMap[baseName].first;
    if(nameWithSpaces.isEmpty()) {
      baseName = NameTools::getNameWithSpaces(baseName);
    } else {
      baseName = nameWithSpaces;
    }
  } else if(config->platform == "scummvm") {
    baseName = NameTools::getScummName(baseName, config->scummIni);
  } else if((config->platform == "neogeo" ||
	     config->platform == "arcade" ||
	     config->platform == "mame-advmame" ||
	     config->platform == "mame-libretro" ||
	     config->platform == "mame-mame4all" ||
	     config->platform == "fba") && !config->mameMap[baseName].isEmpty()) {
    baseName = config->mameMap[baseName];
  }
  baseName = StrTools::stripBrackets(baseName);
  QList<QString> searchNames;
  searchNames.append(baseName);
  return searchNames;
}
