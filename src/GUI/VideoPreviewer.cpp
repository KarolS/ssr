/*
Copyright (c) 2012-2013 Maarten Baert <maarten-baert@hotmail.com>

This file is part of SimpleScreenRecorder.

SimpleScreenRecorder is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

SimpleScreenRecorder is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with SimpleScreenRecorder.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Global.h"
#include "VideoPreviewer.h"

#include "Logger.h"

QSize CalculateScaledSize(QSize in, QSize out) {
	Q_ASSERT(in.width() > 0 && in.height() > 0);
	if(in.width() <= out.width() && in.height() <= out.height())
		return in;
	if(in.width() * out.height() > out.width() * in.height())
		return QSize(out.width(), (out.width() * in.height() + in.width() / 2) / in.width());
	else
		return QSize((out.height() * in.width() + in.height() / 2) / in.height(), out.height());
}

VideoPreviewer::VideoPreviewer(QWidget* parent)
	: QWidget(parent) {

	{
		SharedLock lock(&m_shared_data);
		lock->m_next_frame_time = SINK_TIMESTAMP_ANY;
		lock->m_is_visible = false;
		lock->m_source_size = QSize(0, 0);
		lock->m_widget_size = QSize(0, 0);
		lock->m_frame_rate = 10;
	}

	setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

	connect(this, SIGNAL(NeedsUpdate()), this, SLOT(update()), Qt::QueuedConnection);

}

VideoPreviewer::~VideoPreviewer() {

	// disconnect
	ConnectVideoSource(NULL);

}

void VideoPreviewer::Reset() {
	SharedLock lock(&m_shared_data);
	lock->m_image = QImage();
	emit NeedsUpdate();
}

void VideoPreviewer::SetFrameRate(unsigned int frame_rate) {
	SharedLock lock(&m_shared_data);
	lock->m_frame_rate = std::max(1u, frame_rate);
}

int64_t VideoPreviewer::GetNextVideoTimestamp() {
	SharedLock lock(&m_shared_data);
	return lock->m_next_frame_time;
}

void VideoPreviewer::ReadVideoFrame(unsigned int width, unsigned int height, const uint8_t* data, int stride, PixelFormat format, int64_t timestamp) {
	Q_UNUSED(timestamp);

	QSize image_size;
	{
		SharedLock lock(&m_shared_data);

		// don't do anything if the preview window is invisible
		if(!lock->m_is_visible)
			return;

		// check the size
		if(width < 2 || height < 2 || lock->m_widget_size.width() < 2 || lock->m_widget_size.height() < 2)
			return;

		// check the timestamp
		if(lock->m_next_frame_time == SINK_TIMESTAMP_ANY) {
			lock->m_next_frame_time = timestamp + 1000000 / lock->m_frame_rate;
		} else {
			if(timestamp < lock->m_next_frame_time - 1000000 / lock->m_frame_rate)
				return;
			lock->m_next_frame_time = std::max(lock->m_next_frame_time + 1000000 / lock->m_frame_rate, timestamp);
		}

		// calculate the scaled size
		lock->m_source_size = QSize(width, height);
		image_size = CalculateScaledSize(lock->m_source_size, lock->m_widget_size);

	}

	// allocate the image
	QImage image(image_size, QImage::Format_RGB32);

	// scale the image
	uint8_t *out_data = image.bits();
	int out_stride = image.bytesPerLine();
	m_fast_scaler.Scale(width, height, &data, &stride, format,
						image_size.width(), image_size.height(), &out_data, &out_stride, PIX_FMT_BGRA);

	// set the alpha channel to 0xff (just to be sure)
	// Some applications (e.g. firefox) generate alpha values that are not 0xff.
	// I'm not sure whether Qt cares about this, apparently Qt 4.8 with the 'native' back-end doesn't,
	// but I'm not sure about the other back-ends.
	for(int y = 0; y < image_size.height(); ++y) {
		uint8_t *row = out_data + out_stride * y;
		for(int x = 0; x < image_size.width(); ++x) {
			row[x * 4 + 3] = 0xff; // third byte is alpha because we're little-endian
		}
	}

	// store the image
	SharedLock lock(&m_shared_data);
	lock->m_image = image; image = QImage();

	emit NeedsUpdate();

}

void VideoPreviewer::showEvent(QShowEvent* event) {
	Q_UNUSED(event);
	SharedLock lock(&m_shared_data);
	lock->m_is_visible = true;
}

void VideoPreviewer::hideEvent(QHideEvent *event) {
	Q_UNUSED(event);
	SharedLock lock(&m_shared_data);
	lock->m_is_visible = false;
}

void VideoPreviewer::resizeEvent(QResizeEvent* event) {
	Q_UNUSED(event);
	SharedLock lock(&m_shared_data);
	lock->m_widget_size = QSize(width() - 2, height() - 2);
}

void VideoPreviewer::paintEvent(QPaintEvent* event) {
	Q_UNUSED(event);
	QPainter painter(this);

	// Copy the image so the lock isn't held while actually drawing the image.
	// This is fast because QImage is reference counted.
	QImage img;
	QSize source_size;
	{
		SharedLock lock(&m_shared_data);
		img = lock->m_image;
		source_size = lock->m_source_size;
	}

	if(!img.isNull()) {

		// draw the image
		// Scaling is only used if the widget was resized after the image was captured, which is unlikely
		// except when the video is paused. That's good because the quality after Qt's scaling is horrible.
		QSize out_size = CalculateScaledSize(source_size, QSize(width() - 2, height() - 2));
		QPoint out_pos((width() - out_size.width()) / 2, (height() - out_size.height()) / 2);
		QRect out_rect(out_pos, out_size);
		painter.drawImage(out_rect, img);

		// draw the border
		painter.setPen(Qt::black);
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(out_rect.adjusted(-1, -1, 0, 0));

	}

}
