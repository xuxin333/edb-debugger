/*
Copyright (C) 2006 - 2015 Evan Teran
                          evan.teran@gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DATAVIEWINFO_20100101_H_
#define DATAVIEWINFO_20100101_H_

#include <memory>
#include <QtGlobal>

class QHexView;
class RegionBuffer;
class IRegion;

class DataViewInfo {
public:
	explicit DataViewInfo(const std::shared_ptr<IRegion> &r);
	DataViewInfo();
	~DataViewInfo() = default;

private:
	Q_DISABLE_COPY(DataViewInfo)

public:
	std::shared_ptr<IRegion>      region;
	std::shared_ptr<QHexView>     view;
	std::unique_ptr<RegionBuffer> stream;

public:
	void update();
};

#endif

