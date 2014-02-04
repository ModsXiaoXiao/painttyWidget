#include "waterbased.h"
#include <QtGlobal>
#include <functional>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QBitmap>
#include <QObject>
#include <QDebug>
#include <cmath>

#include "../../misc/shortcutmanager.h"
#include "../../misc/singleton.h"

typedef BrushFeature::LIMIT BFL;

static QColor w_transparent = QColor(255, 255, 255 ,0);

WaterBased::WaterBased():
    BasicBrush(),
    water_(50),
    extend_(50),
    mixin_(20),
    color_remain_(255)
{
    typedef BrushFeature BF;
    BF::FeatureBits bits;
    bits.set(BF::WIDTH);
    bits.set(BF::COLOR);
    bits.set(BF::THICKNESS);
    bits.set(BF::WATER);
    bits.set(BF::EXTEND);
    bits.set(BF::MIXIN);
    features_ = bits;

    name_ = "WaterBrush";
    displayName_ = QObject::tr("WaterBrush");
    shortcut_ = Singleton<ShortcutManager>::instance()
            .shortcut("waterbrush")["key"].toString();
}
int WaterBased::water() const
{
    return water_;
}

void WaterBased::setWater(int water)
{
    water_ = boundValueSet<int>(BFL::WATER_MIN, water, BFL::WATER_MAX);
}
int WaterBased::extend() const
{
    return extend_;
}

void WaterBased::setExtend(int extend)
{
    extend_ = boundValueSet<int>(BFL::EXTEND_MIN, extend, BFL::EXTEND_MAX);
}
int WaterBased::mixin() const
{
    return mixin_;
}

void WaterBased::setMixin(int mixin)
{
    mixin_ = boundValueSet<int>(BFL::MIXIN_MIN, mixin, BFL::MIXIN_MAX);
}

// non-reentrant.
static inline QImage circle_mask(const int width)
{
    static QImage mask;
    if(mask.width() != width || mask.isNull()){
        mask = QImage(width, width, QImage::Format_ARGB32_Premultiplied);
        mask.fill(w_transparent);
        QPainter painter;
        // draw mask
        painter.begin(&mask);
        QPen pen(w_transparent);
        painter.setPen(pen);
        QBrush brush(Qt::black);
        painter.setBrush(brush);
        painter.drawEllipse(0, 0, width, width);
        painter.end();
    }
    return mask;
}

// non-reentrant.
static inline QRgb avg_rgb(const QImage& square)
{
    QRegion region(0, 0, square.width(), square.height(), QRegion::Ellipse);
    bool isPremultiplied = square.format() == QImage::Format_ARGB32_Premultiplied;
    QRgb target = qRgba(255, 255, 255, 0);
    const QRgb* img_data = (const QRgb*) square.constBits();
    const size_t img_data_len = square.byteCount() >> (sizeof(QRgb)>>1);

    unsigned int r_sum = 0;
    unsigned int g_sum = 0;
    unsigned int b_sum = 0;
    unsigned int a_sum = 0;
    unsigned int a_calc_sum = 0;

    unsigned int r = 255;
    unsigned int g = 255;
    unsigned int b = 255;
    unsigned int a = 0;
    unsigned int a_calc = 0;

    size_t colored = 0;
    for(size_t i = 0;i<img_data_len;++i){
        if(!region.contains(QPoint(i/square.width(), i%square.width()))){
            continue;
        }
        QRgb next = *(img_data+i);
        a_calc = a = qAlpha(next);
        if(!a){
            r = 255;
            g = 255;
            b = 255;
            a_calc = 255;
        }else{
            if(isPremultiplied){
                r = qRed(next);
                g = qGreen(next);
                b = qBlue(next);
            }else{
                r = qRed(next)*a/255;
                g = qGreen(next)*a/255;
                b = qBlue(next)*a/255;
            }
        }

        ++colored;

        r_sum += r;
        g_sum += g;
        b_sum += b;
        a_sum += a;
        a_calc_sum += a_calc;
    }
    target = qRgba(r_sum *255/a_calc_sum ,
                   g_sum *255/a_calc_sum,
                   b_sum *255/a_calc_sum,
                   a_sum / colored);
    return target;
}

static inline QColor watering_color(int water, QColor color)
{
    //    auto proc = [&water](int v) -> int {
    //        return qBound(0,
    //                      v + water*255 / 100,
    //                      255);
    //    };

    //    int r = proc(color.red());
    //    int g = proc(color.green());
    //    int b = proc(color.blue());
    //    return QColor(r, g, b, color.alpha());

    color.setAlpha(water*color.alpha() / 100);
    return color;
}

QColor WaterBased::fetchColor(const QPoint& center) const
{
    const int delta_width = width_ >>1;
    const QPoint start_point = std::move(center - QPoint(delta_width, delta_width));

    QImage square = surface_->imagePtr()->copy(QRect(start_point, QSize(width_, width_)));

    QImage&& mask = circle_mask(square.width());
    QPainter painter;
    // composite
    painter.begin(&square);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(0, 0, mask);
    painter.end();

    const QRgb c = avg_rgb(square);
    return QColor::fromRgba(c);
}

int WaterBased::mingleValue(int a, int b) const
{
    int r = qBound(0,
                   (a * color_remain_ + b * (255 - color_remain_)) / 255,
                   255);
    return r;
}

template<typename COLOR_FUNC>
int WaterBased::mingleSubColor(const QColor& color1,
                               const QColor& color2,
                               COLOR_FUNC f) const
{
    using namespace std;
    const auto color_fun = [&f](const QColor& color) -> int{
        const auto& func = bind(f, placeholders::_1);
        return func(color);
    };

    return mingleValue(color_fun(color1), color_fun(color2));
}

QColor WaterBased::mingleColor(const QColor &new_c)
{
    const int r = mingleSubColor
            <decltype(&QColor::red)>(mingled_color_, new_c, &QColor::red);
    const int g = mingleSubColor
            <decltype(&QColor::green)>(mingled_color_, new_c, &QColor::green);
    const int b = mingleSubColor
            <decltype(&QColor::blue)>(mingled_color_, new_c, &QColor::blue);

    return mingled_color_ = QColor(r, g, b, new_c.alpha());
}
static inline QColor mixin_color(QColor brush_color, QColor paper_color, int mixin)
{
//    mixin *= 10;
    int r = qBound(0,
                   (paper_color.red() * mixin + brush_color.red() * (100-mixin)) / 100,
                   255);
    int g = qBound(0,
                   (paper_color.green() * mixin + brush_color.green() * (100-mixin)) / 100,
                   255);
    int b = qBound(0,
                   (paper_color.blue() * mixin + brush_color.blue() * (100-mixin)) / 100,
                   255);
    return QColor(r, g, b);
}
void WaterBased::drawPoint(const QPoint &p, qreal )
{
    last_color_ = fetchColor(p);
    last_point_ = p;
    color_remain_ = 255;
    //    auto watered_color = watering_color(water_, color_);
    last_color_ = mingled_color_ = mixin_color(last_color_, color_, mixin_);
}

void WaterBased::drawLineTo(const QPoint &end, qreal presure)
{
    auto p = end - last_point_;
    int length = (int)sqrt(pow(p.x(), 2) + pow(p.y(), 2));
    color_remain_ -= length / 2 * (100-extend_)/100;
    color_remain_ = qBound(0, color_remain_, 255);
//        auto watered_color = watering_color(water_, color_);
    makeStencil(mingleColor(color_));
    //    makeStencil(watered_color);
    BasicBrush::drawLineTo(end, presure);
}

void WaterBased::setSettings(const BrushSettings &settings)
{
    const auto& s = settings;
    setWater(s.value("water", water_).toInt());
    setExtend(s.value("extend", extend_).toInt());
    setMixin(s.value("mixin", mixin_).toInt());
    BasicBrush::setSettings(s);
}

BrushSettings WaterBased::defaultSettings() const
{
    auto s = BasicBrush::defaultSettings();
    s.insert("water", 50);
    s.insert("extend", 50);
    s.insert("mixin", 20);
    return s;
}

AbstractBrush *WaterBased::createBrush()
{
    return new WaterBased;
}
