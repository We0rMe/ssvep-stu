#include "StimulusWindow.h"
#include "ImpedancePanel.h"
#include "SSVEPStimulusWidget.h"
// Explicit template instantiation permits read-only test access without changing
// production headers or MSVC member-function access-level name decoration.
template<class Tag, typename Tag::Type Member> struct Access {
    friend typename Tag::Type member(Tag) { return Member; }
};
struct StudentRect { using Type=QRectF(StimulusWindow::*)(int)const; friend Type member(StudentRect); };
template struct Access<StudentRect,&StimulusWindow::targetRect>;
struct ImpedanceValue { using Type=void(ImpedancePanel::*)(const QString&,double); friend Type member(ImpedanceValue); };
template struct Access<ImpedanceValue,&ImpedancePanel::updateElectrode>;
struct OriginalLayout { using Type=void(SSVEPStimulusWidget::*)(); friend Type member(OriginalLayout); };
template struct Access<OriginalLayout,&SSVEPStimulusWidget::updateGridLayout>;
struct OriginalRect { using Type=QRectF(SSVEPStimulusWidget::*)(const Target&)const; friend Type member(OriginalRect); };
template struct Access<OriginalRect,&SSVEPStimulusWidget::worldRectToPixelRect>;
struct OriginalBrightness { using Type=float(SSVEPStimulusWidget::*)(const Target&,double)const; friend Type member(OriginalBrightness); };
template struct Access<OriginalBrightness,&SSVEPStimulusWidget::calculateBrightness>;
QRectF studentRect(const StimulusWindow& w,int i){return (w.*member(StudentRect{}))(i);}
void impedanceValue(ImpedancePanel& w,const QString& n,double v){(w.*member(ImpedanceValue{}))(n,v);}
void originalLayout(SSVEPStimulusWidget& w){(w.*member(OriginalLayout{}))();}
QRectF originalRect(const SSVEPStimulusWidget& w,const Target& t){return (w.*member(OriginalRect{}))(t);}
float originalBrightness(const SSVEPStimulusWidget& w,const Target& t,double s){return (w.*member(OriginalBrightness{}))(t,s);}
