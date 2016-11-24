/** Oi Camera Class
 *
 * @copyright	2015 Steffen Vogel
 * @license     http://www.gnu.org/licenses/gpl.txt GNU Public License
 * @author      Steffen Vogel <post@steffenvogel.de>
 * @link        https://dev.0l.de/wiki/projects/omd/start
 * @package     libqt-omd
 */
/*
 * This file is part of libqt-omd
 *
 * libqt-omd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * libqt-omd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libqt-omd. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QtDebug>
#include <QUrl>
#include <QUrlQuery>
#include <QTcpSocket>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFileInfo>

#include "image.h"
#include "properties.h"
#include "camera.h"
#include "helpers.h"

using namespace Oi;

const QString Camera::cUserAgent = "libqt-omd v0.1";

Camera::Camera() :
    mAddress("192.168.0.10"),
    mCamProperties(this),
    mCamMode(MODE_UNKNOWN),
    mConnectMode(CONNECT_UNKNOWN)
{
    connect(&mNetworkManager, &QNetworkAccessManager::finished, this, &Camera::requestFinished);
}

void Camera::initialize()
{
    requestConnectMode();
    requestCamInfo();
    requestCommands();

    switchCamMode(MODE_PLAY);
    completePendingRequests();

    requestCapacity();
    requestImages();

    switchCamMode(MODE_RECORD);
    completePendingRequests();

    requestProperties();
    completePendingRequests();
}

bool Camera::isOnline()
{
    QTcpSocket socket;
    socket.connectToHost(mAddress, 80, QIODevice::ReadOnly);
    return socket.waitForConnected(2000);
}

/********* Request handling ***********/

QNetworkRequest Camera::makeRequest(QString cgi, QMap<QString, QString> params)
{
    QString tpl = QString("http://%1/%2.cgi").arg(mAddress.toString()).arg(cgi);

    QUrl url(tpl);
    QUrlQuery query(url);
    QList<QPair<QString, QString>> paramList;

    for (QString key : params.keys())
        paramList.push_back(qMakePair(key, params[key]));

    if (cgi == "switch_cammode") {
        QPair<QString, QString> tmp = paramList.takeFirst();
        paramList.append(tmp);
    }

    query.setQueryItems(paramList);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, cUserAgent);

    return request;
}

QNetworkReply * Camera::get(QString cgi, QMap<QString, QString> params)
{
    QNetworkRequest request = makeRequest(cgi, params);
    QNetworkReply *reply = mNetworkManager.get(request);

    qDebug() << "[libqt-omd] New GET request:" << request.url();

    mPendingReplies.append(reply);
    return reply;
}

QNetworkReply * Camera::post(QString cgi, QMap<QString, QString> params, QDomDocument body)
{
    QNetworkRequest request = makeRequest(cgi, params);
    QNetworkReply *reply = mNetworkManager.post(request, body.toByteArray());

    qDebug() << "[libqt-omd] New POST request:" << request.url();

    mPendingReplies.append(reply);
    return reply;
}

void Camera::completePendingRequests()
{
    while (!mPendingReplies.empty())
        mLoop.exec();
}

void Camera::requestFinished(QNetworkReply *reply)
{
    qDebug() << "[libqt-omd] Request finished:" << reply->url();

    if (reply->error() == QNetworkReply::NoError) {
        QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        QString cgi = QFileInfo(reply->url().fileName()).baseName();
        QMap<QString, QString> params = Oi::pairsToMap(QUrlQuery(reply->url()).queryItems());

        if (contentType == "text/xml"         && reply->size() > 0) {
            QDomDocument body;
            if (body.setContent(reply->readAll())) {
                //qDebug() << "[libqt-omd]   Content:" <<  body.toString();
                parseXml(cgi, body);
            }
            else
                qCritical() << "Failed to parse XML reply";
        }
        else if (contentType == "text/plain"  && reply->size() > 0)
            parseList(cgi, reply->readAll());
        else if (contentType == "www/unknown" && reply->size() == 0)
            parseEmpty(cgi, reply);
        else if (contentType == "image/jpeg"  && reply->size() > 0)
            parseImage(cgi, reply->readAll());
        else
            qCritical() << "Failed to parse reply: Content-Type = " << contentType
                        << ", Size = " << reply->size()
                        << ", URI  = " << reply->request().url();
    }
    else
       qWarning() << "Request failed: " << reply->errorString();

    mPendingReplies.removeAll(reply);
    mLoop.exit();
}

/********* Actions ***********/

void Camera::controlZoom(ZoomMode cmd)
{
    Q_UNUSED(cmd)
    // FIXME implement
}

void Camera::setTimeDiff(QDateTime t)
{
    Q_UNUSED(t)
    // FIXME implement
}

void Camera::reFocus(QPoint pos, QSize size)
{
    Q_UNUSED(pos)
    Q_UNUSED(size)
    // FIXME implement
}

/********* Requests **********/

void Camera::takeShot()                 { get("exec_takemotion"); }
void Camera::powerOff()                 { get("exec_pwoff"); }

void Camera::requestCamInfo()           { get("get_caminfo"); }
void Camera::requestCapacity()          { get("get_unusedcapacity"); }
void Camera::requestConnectMode()       { get("get_connectmode"); }
void Camera::requestCommands()          { get("get_commandlist"); }

void Camera::requestProperties()
{
    QMap<QString, QString> params;

    params["com"] = "desc";
    params["propname"] = "desclist";

    get("get_camprop", params);

}

void Camera::requestImages(QString dir, bool rsv) {
    QMap<QString, QString> params;

    params["DIR"] = dir.replace('/', "%2F");

    get(rsv ? "get_rsvimglist" : "get_imglist", params);
}

void Camera::requestImage(QString name, QSize resolution)
{
    Q_UNUSED(resolution)

    QString tpl = QString("http://%1/DCIM/100OLYMP/%1.JPG").arg(mAddress.toString()).arg(name);
    QUrl url(tpl);
    QNetworkRequest request(url);

    mNetworkManager.get(request);
}

void Camera::switchCamMode(CamMode mode)
{
    QMap<QString, QString> params;

    switch (mode) {
        case MODE_PLAY:
            params["mode"] = "play";
            break;

        case MODE_RECORD:
            params["mode"] = "rec";
            params["lvqty"] = "0320x0240"; // FIXME allow other liveViewQualities
            break;

        case MODE_SHUTTER:
            params["mode"] = "shutter";
            break;

        case MODE_UNKNOWN:
        default:
            return;
    }

    QNetworkReply *reply = get("switch_cammode", params);
    reply->setProperty("camMode", QVariant(mode));
}

/*********** Reply parsers ************/

void Camera::parseXml(QString cgi, QDomDocument body)
{
    qDebug() << "[libqt-omd] Parsing XML for:" << cgi;

    if      (cgi == "get_unusedcapacity") parseCapacity(body);
    else if (cgi == "get_commandlist")    parseCommandList(body);
    else if (cgi == "get_caminfo")        parseCamInfo(body);
    else if (cgi == "get_camprop")        parseProperties(body);
    else if (cgi == "set_camprop")        parseProperties(body);
    else if (cgi == "get_connectmode")    parseConnectMode(body);
    else if (cgi == "exec_takemotion")    parseTakeMotion(body);
    else if (cgi == "exec_takemisc")      parseTakeMisc(body);
}

void Camera::parseList(QString cgi, QByteArray body)
{
    bool mark = (cgi == "get_rsvimglist");

    QStringList lines = QString(body).split("\r\n");
    QString header = lines.takeFirst();

    qDebug() << "[libqt-omd] Version Header:" << header;

    for (QString line : lines) {
        if (line.size()) {
            Image img(line, mark, this);
            mImages.insert(img.path(), img);
        }
    }

    emit imagesUpdated(mImages);
}

void Camera::parseImage(QString cgi, QByteArray body)
{
    Q_UNUSED(cgi)

    QImage img = QImage::fromData(body.data());

    emit receivedImage(img);
}

void Camera::parseEmpty(QString cgi, QNetworkReply *reply)
{
    if (cgi == "switch_cammode") {
        mCamMode = static_cast<CamMode>(reply->property("camMode").toInt());
        emit changedMode(mCamMode);
    }
    else if (cgi == "exec_pwoff")
        emit poweredOff();
}

void Camera::parseCamInfo(QDomDocument body)
{
    QDomElement elm = body.firstChildElement("caminfo");
    if (!elm.isNull()) {
        QDomElement model = elm.firstChildElement("model");
        if (!model.isNull()) {
            mCamModel = model.text();

            emit modelUpdated(mCamModel);
        }
    }
}

void Camera::parseCapacity(QDomDocument body)
{
    QDomElement elm = body.firstChildElement("unused");
    if (!elm.isNull()) {
        mUnusedCapacity = elm.text().toULong();

        emit capacityUpdated(mUnusedCapacity);
    }
}

void Camera::parseCommandList(QDomDocument body)
{
    mCommandList = body;
}

void Camera::parseConnectMode(QDomDocument body)
{
    QDomElement elm = body.firstChildElement("connectmode");
    if (!elm.isNull()) {
        if (elm.text() == "private")
            mConnectMode = CONNECT_PRIVATE;
        else if (elm.text() == "shared")
            mConnectMode = CONNECT_SHARED;
        else
            mConnectMode = CONNECT_UNKNOWN;

        emit connected(mConnectMode);
    }
}

void Camera::parseProperties(QDomDocument body)
{
    QDomElement elm = body.firstChildElement("desclist");
    if (!elm.isNull()) {
        mCamProperties.parse(elm);
        emit propertiesUpdated(&mCamProperties);
    }
}

void Camera::parseTakeMisc(QDomDocument body)
{
    Q_UNUSED(body)
    // FIXME implement
}

void Camera::parseTakeMotion(QDomDocument body)
{
    Q_UNUSED(body)
    // FIXME implement
}
