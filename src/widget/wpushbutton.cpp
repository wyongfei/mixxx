/***************************************************************************
                          wpushbutton.cpp  -  description
                             -------------------
    begin                : Fri Jun 21 2002
    copyright            : (C) 2002 by Tue & Ken Haste Andersen
    email                : haste@diku.dk
***************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "wpushbutton.h"
#include "wpixmapstore.h"
#include "controlobject.h"
#include "controlobjectthread.h"
#include "controlpushbutton.h"
#include "control/controlbehavior.h"
//Added by qt3to4:
#include <QPixmap>
#include <QtDebug>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QPaintEvent>
#include <QApplication>

const int PB_SHORTKLICKTIME = 200;

WPushButton::WPushButton(QWidget * parent)
        : WWidget(parent),
          m_pPixmaps(NULL),
          m_pPixmapBack(NULL),
          m_leftButtonMode(ControlPushButton::PUSH),
          m_rightButtonMode(ControlPushButton::PUSH) {
    setStates(0);
    setAttribute(Qt::WA_AcceptTouchEvents);
    m_pTouchShift = new ControlObjectThread("[Controls]", "touch_shift");
}

WPushButton::~WPushButton() {
    for (int i = 0; i < 2*m_iNoStates; i++) {
        WPixmapStore::deletePixmap(m_pPixmaps[i]);
    }
    delete [] m_pPixmaps;
    WPixmapStore::deletePixmap(m_pPixmapBack);

    delete m_pTouchShift;
}

void WPushButton::setup(QDomNode node) {
    // Number of states
    int iNumStates = selectNodeInt(node, "NumberStates");
    setStates(iNumStates);

    // Set background pixmap if available
    if (!selectNode(node, "BackPath").isNull()) {
        setPixmapBackground(getPath(selectNodeQString(node, "BackPath")));
    }

    // Load pixmaps for associated states
    QDomNode state = selectNode(node, "State");
    while (!state.isNull()) {
        if (state.isElement() && state.nodeName() == "State") {
            setPixmap(selectNodeInt(state, "Number"), true, getPath(selectNodeQString(state, "Pressed")));
            setPixmap(selectNodeInt(state, "Number"), false, getPath(selectNodeQString(state, "Unpressed")));
        }
        state = state.nextSibling();
    }

    m_bLeftClickForcePush = selectNodeQString(node, "LeftClickIsPushButton")
            .contains("true", Qt::CaseInsensitive);

    if (!selectNodeQString(node, "RightClickIsPushButton").isEmpty()) {
        qDebug() << "using <RightClickIsPushButton> in skins is obsolete.";
    }

    QDomNode con = selectNode(node, "Connection");
    while (!con.isNull()) {
        // Get ConfigKey
        QString key = selectNodeQString(con, "ConfigKey");

        ConfigKey configKey;
        configKey.group = key.left(key.indexOf(","));
        configKey.item = key.mid(key.indexOf(",")+1);

        ControlPushButton* p = dynamic_cast<ControlPushButton*>(
            ControlObject::getControl(configKey));

        if (p == NULL) {
            // A NULL here either means that this control is not a
            // ControlPushButton or it does not exist. This logic is
            // specific to push-buttons, so skip it either way.
            con = con.nextSibling();
            continue;
        }

        bool isLeftButton = false;
        bool isRightButton = false;
        if (!selectNode(con, "ButtonState").isNull()) {
            if (selectNodeQString(con, "ButtonState").contains("LeftButton", Qt::CaseInsensitive)) {
                isLeftButton = true;
            } else if (selectNodeQString(con, "ButtonState").contains("RightButton", Qt::CaseInsensitive)) {
                isRightButton = true;
            }
        }

        // Based on whether the control is mapped to the left or right button,
        // record the button mode.
        if (isLeftButton) {
            m_leftButtonMode = p->getButtonMode();
        } else if (isRightButton) {
            m_rightButtonMode = p->getButtonMode();
        }
        con = con.nextSibling();
    }
}

void WPushButton::setStates(int iStates) {
    m_iNoStates = iStates;
    m_fValue = 0.;
    m_leftPressed = false;
    m_rightPressed = false;
    m_activeTouchButton = Qt::NoButton;

    // If pixmap array is already allocated, delete it
    delete [] m_pPixmaps;
    m_pPixmaps = NULL;

    if (iStates > 0) {
        m_pPixmaps = new QPixmap*[2 * m_iNoStates];
        for (int i = 0; i < (2 * m_iNoStates); ++i) {
            m_pPixmaps[i] = NULL;
        }
    }
}

void WPushButton::setPixmap(int iState, bool bPressed, const QString &filename) {
    int pixIdx = (iState * 2) + (bPressed ? 1 : 0);
    if (pixIdx < 2 * m_iNoStates) {
        m_pPixmaps[pixIdx] = WPixmapStore::getPixmap(filename);
        if (!m_pPixmaps[pixIdx]) {
            qDebug() << "WPushButton: Error loading pixmap:" << filename;
        } else {
            // Set size of widget equal to pixmap size
            setFixedSize(m_pPixmaps[pixIdx]->size());
        }
    }
}

void WPushButton::setPixmapBackground(const QString &filename) {
    // Load background pixmap
    m_pPixmapBack = WPixmapStore::getPixmap(filename);
    if (!m_pPixmapBack) {
        qDebug() << "WPushButton: Error loading background pixmap:" << filename;
    }
}

void WPushButton::setValue(double v) {
    m_fValue = v;

    if (m_iNoStates == 1) {
        if (v == 0.0) {
            m_leftPressed = false;
        } else {
            m_leftPressed = true;
        }
    }
    update();
}

bool WPushButton::event(QEvent* e) {
    // control events when disabled
    if (isEnabled()) {
        switch(e->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        {
            QTouchEvent* touchEvent = static_cast<QTouchEvent*>(e);
            if (touchEvent->deviceType() !=  QTouchEvent::TouchScreen) {
                break;
            }

            // fake a mouse event!
            QEvent::Type eventType = QEvent::None;
            switch (touchEvent->type()) {
            case QEvent::TouchBegin:
                eventType = QEvent::MouseButtonPress;
                if (m_pTouchShift->get() != 0.0) {
                    // touch is right click
                    m_activeTouchButton = Qt::RightButton;
                } else {
                    m_activeTouchButton = Qt::LeftButton;
                }
                break;
            case QEvent::TouchUpdate:
                eventType = QEvent::MouseMove;
                break;
            case QEvent::TouchEnd:
                eventType = QEvent::MouseButtonRelease;
                break;
            default:
                Q_ASSERT(!true);
                break;
            }

            const QTouchEvent::TouchPoint &touchPoint =
                    touchEvent->touchPoints().first();
            QMouseEvent mouseEvent(eventType,
                    touchPoint.pos().toPoint(),
                    touchPoint.screenPos().toPoint(),
                    m_activeTouchButton, // Button that causes the event
                    Qt::NoButton, // Not used, so no need to fake a proper value.
                    touchEvent->modifiers());

            return QWidget::event(&mouseEvent);
        }
        default:
            break;
        }
    }

    return QWidget::event(e);
}

void WPushButton::paintEvent(QPaintEvent *) {
    if (m_iNoStates > 0)     {
        int idx = ((int)m_fValue % m_iNoStates) * 2;
        if (m_leftPressed || m_rightPressed) {
            ++idx;
        }
        if (m_pPixmaps[idx]) {
            QPainter p(this);
            if(m_pPixmapBack) {
                p.drawPixmap(0, 0, *m_pPixmapBack);
            }
            p.drawPixmap(0, 0, *m_pPixmaps[idx]);
        }
    }
}

void WPushButton::mousePressEvent(QMouseEvent * e) {
    const bool leftClick = e->button() == Qt::LeftButton;
    const bool rightClick = e->button() == Qt::RightButton;

    const bool leftPowerWindowStyle = m_leftButtonMode == ControlPushButton::POWERWINDOW;
    if (leftPowerWindowStyle && m_iNoStates == 2) {
        if (leftClick) {
            if (m_fValue == 0.0f) {
                m_clickTimer.setSingleShot(true);
                m_clickTimer.start(ControlPushButtonBehavior::kPowerWindowTimeMillis);
            }
            m_fValue = 1.0f;
            m_leftPressed = true;
            emit(valueChangedLeftDown(1.0f));
            update();
        } else if (rightClick) {
            // Latching in power window mode
            m_leftPressed = false;
        }
        return;
    }

    if (rightClick) {
        // This is the secondary button function, it does not change m_value
        // Due the leak of visual feedback the right button is always a
        // pushbutton so "RightClickIsPushButton" is obsolete
        m_rightPressed = true;
        emit(valueChangedRightDown(1.0f));
        update();

        // Do not allow right-clicks to change button state other than when
        // forced to be a push button. This is how Mixxx <1.8.0 worked so
        // keep it that way. For a multi-state button, really only one click
        // type (left/right) should be able to change the state. One problem
        // with this is that you can get the button out of sync with its
        // underlying control. For example the PFL buttons on Jus's skins
        // could get out of sync with the button state. rryan 9/2010
        return;
    }

    if (leftClick) {
        double emitValue;
        if (m_bLeftClickForcePush) {
            // This may a button with different functions on each mouse button
            // m_fValue is changed by a separate feedback connection
            emitValue = 1.0f;
        } else if (m_iNoStates == 1) {
            // This is a Pushbutton
            m_fValue = emitValue = 1.0f;
        } else {
            // Toggle thru the states
            m_fValue = emitValue = (int)(m_fValue+1.)%m_iNoStates;
        }
        m_leftPressed = true;
        emit(valueChangedLeftDown(emitValue));
        update();
    }
}

void WPushButton::mouseReleaseEvent(QMouseEvent * e) {
    const bool leftClick = e->button() == Qt::LeftButton;
    const bool rightClick = e->button() == Qt::RightButton;
    const bool leftPowerWindowStyle = m_leftButtonMode == ControlPushButton::POWERWINDOW;

    if (leftPowerWindowStyle && m_iNoStates == 2) {
        if (leftClick) {
            if (m_leftPressed && !m_rightPressed && !m_clickTimer.isActive()) {
                // Release Button after Timer, but not if right button is clicked
                m_fValue = 0.0f;
                emit(valueChangedLeftUp(0.0f));
            }
            m_leftPressed = false;
        } else if (rightClick) {
            // right click is latching
            m_leftPressed = false;
        }
        update();
        return;
    }

    if (rightClick) {
        // This is the secondary clickButton function, it does not change
        // m_value due the leak of visual feedback we do not allow a toggle
        // function. It is always a pushbutton, so "RightClickIsPushButton"
        // is obsolete
        m_rightPressed = false;
        emit(valueChangedRightUp(0.0f));
        update();
        return;
    }

    if (leftClick) {
        double emitValue = m_fValue;
        if (m_bLeftClickForcePush) {
            // This may a klickButton with different functions on each mouse button
            // m_fValue is changed by a separate feedback connection
            emitValue = 0.0f;
        } else if (m_iNoStates == 1) {
            // This is a Pushbutton
            m_fValue = emitValue = 0.0f;
        } else {
            // Nothing special happens when releasing a toggle button
        }
        m_leftPressed = false;
        emit(valueChangedLeftDown(emitValue));
        update();
    }
}

void WPushButton::focusOutEvent(QFocusEvent* e) {
    qDebug() << "WPushButton::focusOutEvent" << e->reason();
    if (e->reason() != Qt::MouseFocusReason) {
        // Since we support multi touch there is no reason to reset
        // the pressed flag if the Primary touch point is moved to an
        // other widget
        m_leftPressed = false;
        m_rightPressed = false;
        update();
    }
}
