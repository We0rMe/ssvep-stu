#include <QtWidgets>
#include <QtTest>
#include <Eigen/Dense>
#include <array>
#include <memory>
#include <random>
#include <iostream>
#include <stdexcept>
#define private public
#include "MainWindow.h"
#include "ImpedancePanel.h"
#include "Session.h"
#include "SSVEPOnlineThread.h"
#include "SSVEPStimulusWidget.h"
#undef private
#include "AppStyle.h"
#include "CsvWriter.h"
#include "SSVEPPreprocess.h"
QRectF studentRect(const StimulusWindow&,int);
void impedanceValue(ImpedancePanel&,const QString&,double);
void originalLayout(SSVEPStimulusWidget&);
QRectF originalRect(const SSVEPStimulusWidget&,const Target&);
float originalBrightness(const SSVEPStimulusWidget&,const Target&,double);

static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void pumpUntil(const std::function<bool()>& done, int timeout)
{
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < timeout) QTest::qWait(5);
    check(done(), "Timed out waiting for experiment");
}
static double sampleValue(int target, int ch, int sample)
{
    static const auto targets=makeTargets();
    const auto t=targets[static_cast<size_t>(target-1)];
    const double time=sample/1000.0;
    uint32_t noise=uint32_t(sample)*2654435761u+uint32_t(ch)*2246822519u+uint32_t(target)*3266489917u;
    noise^=noise>>16;noise*=2246822519u;noise^=noise>>13;
    return 20*std::sin(6.283185307179586*t.frequency*time+t.phase+ch*.15)
        + 7*std::cos(12.56637061435917*t.frequency*time+ch*.31)
        + .2*std::sin((target*17+ch*3+1)*time*3.13)+.8*(noise/4294967295.0-.5);
}
int main(int argc,char**argv)
{
    QApplication app(argc,argv);
    app.setStyle(new AppStyle);
    app.setWindowIcon(QIcon(":/branding/assets/logo.png"));
    qRegisterMetaType<QList<EEG_PACKET>>("QList<EEG_PACKET>");
    qRegisterMetaType<QList<EventMarker>>("QList<EventMarker>");
    const QString out=QDir::currentPath();
    try {
        auto mainOwner=std::unique_ptr<MainWindow>(new MainWindow);auto& main=*mainOwner;
        main.show(); QTest::qWait(250);
        check(main.grab().save(out+"/main-window.png"),"Main window snapshot");
        auto* panel=main.m_impedance;
        check(panel->m_electrodeLabels.size()==66 && panel->m_valueLabels.size()==66,"All 66 electrodes and values");
        for(auto* label:panel->m_valueLabels)check(label->text()=="inf","Initial impedance is inf");
        impedanceValue(*panel,"Fp1",19.9); impedanceValue(*panel,"Fp2",20); impedanceValue(*panel,"Oz",50);
        check(panel->m_electrodeLabels["Fp1"]->styleSheet().contains("green"),"Green threshold");
        check(panel->m_electrodeLabels["Fp2"]->styleSheet().contains("yellow"),"Yellow threshold");
        check(panel->m_electrodeLabels["Oz"]->styleSheet().contains("red"),"Red threshold");
        std::cout<<"PASS UI: "<<main.width()<<"x"<<main.height()<<", scalp panel "<<panel->width()<<"x"<<panel->height()<<std::endl;
        main.hide();

        auto originalOwner=std::unique_ptr<SSVEPStimulusWidget>(new SSVEPStimulusWidget);auto& original=*originalOwner;
        original.setAutoShowEnabled(false);
        auto studentOwner=std::unique_ptr<StimulusWindow>(new StimulusWindow);auto& student=*studentOwner;
        const auto targets=makeTargets();
        const auto protocol=SSVEPOfflineThread::createDefaultJFPMStimuli();
        for(int i=0;i<40;++i){
            check(targets[i].frequency==original.m_targets[i].frequency && targets[i].phase==original.m_targets[i].phase,"Exact JFPM coding");
            check(protocol[i].cueDurationMs==500&&protocol[i].flickerDurationMs==1000&&protocol[i].restDurationMs==500,"Original offline durations");
            for(int frame=0;frame<300;++frame){
                const double time=frame*(1.0/60.0);
                const float brightness=static_cast<float>(std::max(0.0,std::min(1.0,.5*(1+std::sin(6.28318530717958647692*targets[i].frequency*time+targets[i].phase)))));
                check(brightness==originalBrightness(original,original.m_targets[i],time),"Exact frame brightness");
            }
        }
        for(const QSize size : {QSize(1920,1080),QSize(1366,768),QSize(2560,1440),QSize(800,1200)}){
            original.resize(size); student.resize(size);
            const float aspect=float(size.width())/size.height();
            original.m_worldLeft=aspect>=1?-aspect:-1;
            original.m_worldRight=-original.m_worldLeft;
            original.m_worldBottom=aspect>=1?-1:-1/aspect;
            original.m_worldTop=-original.m_worldBottom;
            originalLayout(original);
            for(int i=0;i<40;++i){
                const auto a=originalRect(original,original.m_targets[i]);
                const auto b=studentRect(student,i);
                check(std::abs(a.x()-b.x())<1e-9 && std::abs(a.y()-b.y())<1e-9 && std::abs(a.width()-b.width())<1e-9 && std::abs(a.height()-b.height())<1e-9,"Original grid geometry");
            }
        }
        std::cout<<"PASS stimulus: 40 codes, 12000 frame values, 160 target rectangles"<<std::endl;

        QList<EEG_PACKET> packets;
        for(int i=0;i<3;++i){EEG_PACKET p{};p.timestamp=100+2*i;p.voltage.data[0]=i;packets.append(p);}
        QList<EventMarker> markers;markers.append({3001,101,"tie"});markers.append({4001,101,"collision"});
        QString error;
        check(writeSessionCsv(out+"/marker-check.csv",packets,markers,error),"CSV save");
        QFile csv(out+"/marker-check.csv");check(csv.open(QIODevice::ReadOnly),"CSV read");
        const auto csvLines=csv.readAll().split('\n');
        check(csvLines[0].contains("Marker1,Marker2") && csvLines[1].trimmed().endsWith("3001,4001"),"Marker ties select earlier sample, collisions preserved");
        std::cout<<"PASS CSV marker alignment and collision handling"<<std::endl;

        // Three blocks match the default; each validation fold retains two trials
        // per class for cross-trial covariance estimation.
        QFile trainCsv(out+"/synthetic.csv");check(trainCsv.open(QIODevice::WriteOnly|QIODevice::Text),"Synthetic CSV open");
        QTextStream stream(&trainCsv);
        stream<<ssvepChannelNames().join(',')<<",Marker1,Marker2\n";
        for(int block=0;block<3;++block)for(int target=1;target<=40;++target)for(int s=0;s<800;++s){
            for(int ch=0;ch<9;++ch)stream<<sampleValue(target,ch,s)<<',';
            stream<<(s==0&&target==1?1001+block:0)<<','<<(s==0?3000+target:0)<<'\n';
        }
        stream.flush();trainCsv.close();
        SSVEPModelTrainer trainer;SSVEPTrainingReport report;SSVEPTrainingConfig config;std::string trainError;
        check(trainer.trainFromCsv((out+"/synthetic.csv").toStdString(),"synthetic_check",config,report,trainError),trainError.c_str());
        check(report.num_blocks==3 && report.num_smpls==125 && report.model.num_targs==40,"40-class training package");
        check(report.model.W.allFinite() && report.model.trains.allFinite(),"Finite trained model parameters");
        auto decoder=std::make_shared<ModelDecoder>();
        check(decoder->load(QString::fromStdString(report.output_pkg_dir),error),"Load trained package");
        std::cout<<"PASS offline preprocessing, 3-fold training and model package reload"<<std::endl;

        // Replay one live-timed online trial through the actual student UI adapter.
        auto onlineOwner=std::unique_ptr<Session>(new Session);auto& online=*onlineOwner;
        int decoded=0;bool ended=false;QList<EventMarker> onlineMarkers;
        QObject::connect(&online,&Session::sessionEnded,&app,[&](bool,bool,const QList<EEG_PACKET>&,const QList<EventMarker>& m,int,int){ended=true;onlineMarkers=m;});
        QObject::connect(&online,&Session::feedbackUpdated,&app,[&](int,int,int,int predicted,const QString& e){
            check(predicted>=1&&predicted<=40 && e.isEmpty(),"Online prediction with replay samples");++decoded;
        });
        online.start(Session::Mode::Online,1,decoder);
        check(!online.m_online->isHybridAssetsLoaded(),"Pure EEG engine");
        qint64 onset=0;int activeTarget=1;
        QObject::connect(online.m_window,&StimulusWindow::stimulusOnset,&app,[&](int t,qint64 ts){onset=ts;activeTarget=t;});
        QTimer producer;producer.setInterval(5);
        qint64 last=QDateTime::currentMSecsSinceEpoch();
        QObject::connect(&producer,&QTimer::timeout,&app,[&]{
            const qint64 now=QDateTime::currentMSecsSinceEpoch();
            for(qint64 ts=last+1;ts<=now;++ts){EEG_PACKET p{};p.timestamp=ts;
                const auto names=ssvepChannelNames();
                for(int ch=0;ch<9;++ch){int index=electrodeMap.key(names[ch],-1);p.voltage.data[index]=sampleValue(activeTarget,ch,onset?int(ts-onset):0);}
                online.appendPacket(p);
            }last=now;
        });producer.start();
        pumpUntil([&]{return online.m_state==ExperimentState::Instruction;},2000);
        emit online.m_window->continueRequested();
        pumpUntil([&]{return online.m_state==ExperimentState::TrialFeedback;},7000);
        check(online.m_window->m_stage==StimulusWindow::Stage::Feedback,"Feedback UI state");
        pumpUntil([&]{return online.m_state==ExperimentState::Idle;},2000);
        check(online.m_window->m_stage==StimulusWindow::Stage::Blank,"Black inter-trial interval");
        pumpUntil([&]{return decoded==1;},3000);
        online.stop();producer.stop();check(ended,"Online stop saved data");
        qint64 cue=0,start=0,end=0,feedback=0,trialEnd=0;
        for(const auto& m:onlineMarkers){if(!cue&&m.code>=2001&&m.code<=2040)cue=m.timestamp;if(!start&&m.code>=3001&&m.code<=3040)start=m.timestamp;if(!end&&m.code>=4001&&m.code<=4040)end=m.timestamp;if(!feedback&&m.code>=7001&&m.code<=7040)feedback=m.timestamp;if(!trialEnd&&m.code>=5001&&m.code<=5040)trialEnd=m.timestamp;}
        check(start-cue>=490 && end-start>=490 && trialEnd-feedback>=790,"Original online timing minima");
        std::cout<<"PASS online adapter: first-frame marker, complete 130-630 ms data window, prediction, green feedback, 500 ms blank, stop/save"<<std::endl;

        auto offlineOwner=std::unique_ptr<Session>(new Session);auto& offline=*offlineOwner;bool offlineEnded=false;QList<EventMarker> offlineMarkers;
        QObject::connect(&offline,&Session::sessionEnded,&app,[&](bool complete,bool isOnline,const QList<EEG_PACKET>&,const QList<EventMarker>& m,int,int){check(!complete&&!isOnline,"Offline interruption flags");offlineEnded=true;offlineMarkers=m;});
        offline.start(Session::Mode::Offline,2);
        pumpUntil([&]{return offline.m_state==ExperimentState::Instruction;},2000);
        emit offline.m_window->continueRequested();
        pumpUntil([&]{return offline.m_state==ExperimentState::TrialRest;},5000);
        QTest::qWait(530);offline.stop();check(offlineEnded,"Offline adapter stop");
        cue=start=end=trialEnd=0;
        for(const auto& m:offlineMarkers){if(!cue&&m.code>=2001&&m.code<=2040)cue=m.timestamp;if(!start&&m.code>=3001&&m.code<=3040)start=m.timestamp;if(!end&&m.code>=4001&&m.code<=4040)end=m.timestamp;if(!trialEnd&&m.code>=5001&&m.code<=5040)trialEnd=m.timestamp;}
        check(start-cue>=490 && end-start>=990 && trialEnd-end>=490,"Original offline timings");
        std::cout<<"PASS offline adapter: 500/1000/500 ms timing and interruption"<<std::endl;
        std::cout<<"Cleanup: offline session"<<std::endl;offlineOwner.reset();
        std::cout<<"Cleanup: online session"<<std::endl;onlineOwner.reset();
        std::cout<<"Cleanup: pending closed windows"<<std::endl;QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        std::cout<<"Cleanup: student renderer"<<std::endl;studentOwner.reset();
        std::cout<<"Cleanup: original reference renderer"<<std::endl;originalOwner.reset();
        std::cout<<"Cleanup: main window"<<std::endl;mainOwner.reset();
        std::cout<<"ALL CHECKS PASSED"<<std::endl;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<std::endl;return 1;}
    return 0;
}
