#include <M5Unified.h>
#include "M5UnitGLASS2.h" // Glass2ライブラリを使用
#include <vector>

// ---- デバイス定義 ----
M5UnitGLASS2 display(2, 1, 400000); 
M5Canvas canvas; // Glass2への描画バッファとしてCanvasを使用します

// ---- ピン定義 ----
const int swPins[] = {38, 39, 7, 8};
const int numSwitches = 4;
const int ANALOG_PIN = 6; // ボタン入力（抵抗分圧）
const int BUZZER_PIN = 5; // ブザー用出力ピン（デジタル）

// ---- 個別ディレイ設定 ----
// SW1=1500, SW2=1000, SW3=1500, SW4=1200
const int swDelays[] = {1500, 1000, 1500, 1200}; 

// ---- ゲーム用変数 ----
enum GameState { TITLE, PLAYING_SEQUENCE, WAITING_INPUT, GAME_OVER };
GameState currentState = TITLE;

std::vector<int> sequence; // お手本の順番を記憶
int inputIndex = 0;        // プレイヤーが何個目まで入力したか
int lastButton = -1;       // 前回のボタン状態（チャタリング・長押し防止用）
bool stateChanged = true;  // 画面描画のフラグ
int currentRawADC = 0;     // 画面表示用の現在のアナログ値

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // ピンの初期化
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    pinMode(ANALOG_PIN, INPUT); // アナログ入力として明示的に設定

    // Glass2の初期化
    display.init();
    display.setRotation(1);
    canvas.createSprite(128, 64);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(TFT_WHITE);

    // HCT4066の制御ピンを初期化
    for (int i = 0; i < numSwitches; i++) {
        pinMode(swPins[i], OUTPUT);
        digitalWrite(swPins[i], LOW);
    }

    // 突入電流・起動時の電圧不安定ノイズ対策（2秒待機）
    canvas.fillSprite(TFT_BLACK);
    canvas.drawCentreString("SYSTEM BOOT...", 64, 30);
    display.pushImage(0, 0, 128, 64, (uint16_t*)canvas.getBuffer());
    delay(2000);

    // 溜まったノイズデータを空読みして捨てる
    for(int i = 0; i < 10; i++) {
        analogRead(ANALOG_PIN);
        delay(10);
    }
    lastButton = -1; // 完全に安定してから初期化
}

// 物理的なブザーを鳴らすためのカスタム関数（デジタルピンで音程を作る）
void customTone(int pin, int freq, int duration) {
    long halfPeriod = 1000000L / (freq * 2);
    long cycles = ((long)freq * (long)duration) / 1000;
    for (long i = 0; i < cycles; i++) {
        digitalWrite(pin, HIGH);
        delayMicroseconds(halfPeriod);
        digitalWrite(pin, LOW);
        delayMicroseconds(halfPeriod);
    }
}

// ゲームの状況に合わせてブザーを鳴らす関数
void playBuzzer(int type) {
    if (type == 0) {
        // ゲームオーバー: 低く長いブー音 (150Hzで3秒間に延長し、警告感を強調)
        customTone(BUZZER_PIN, 150, 3000);
    } else if (type == 1) {
        // ゲームスタート: 高くなる3連音
        customTone(BUZZER_PIN, 880, 100); delay(50);
        customTone(BUZZER_PIN, 1174, 100); delay(50);
        customTone(BUZZER_PIN, 1760, 300);
    }
}

// PIN6のアナログ値から、どのボタンが押されたか判定する関数（デバウンス超強化版）
int readButton() {
    int raw1 = analogRead(ANALOG_PIN); delay(5);
    int raw2 = analogRead(ANALOG_PIN); delay(5);
    int raw3 = analogRead(ANALOG_PIN);
    
    // ボタンを押し込む途中・離す途中の「不安定な電圧」を完全に弾く
    int diff = raw1 - raw3;
    if (diff < 0) diff = -diff; // 絶対値に変換
    if (diff > 150) return -1;  // 変動が激しい時は無効判定（誤爆防止）

    int raw = (raw1 + raw2 + raw3) / 3;
    currentRawADC = raw; // 画面表示用にグローバル変数へ保存

    // アナログ値（0〜4095）の閾値でボタンを判定
    if (raw > 3000) return -1; // 何も押されていない (約3.3V)
    if (raw < 400)  return 0;  // SW1 (直結: 0Ω)
    if (raw < 1200) return 1;  // SW2 (2.2kΩ または 3.3kΩ)
    if (raw < 1800) return 2;  // SW3 (4.7kΩ)
    if (raw < 2800) return 3;  // SW4 (10kΩ)
    
    return -1;
}

// 指定したスイッチの音と光を再生する関数（プレイヤー入力時の硬直を解消）
void playTarget(int btn, bool isPlayerInput) {
    digitalWrite(swPins[btn], HIGH);
    delay(400); // ガジェットICの反応時間（固定）
    digitalWrite(swPins[btn], LOW);
    
    // プレイヤーが入力した時は待機時間をキャンセルし、連続入力を受け付ける
    if (!isPlayerInput) {
        delay(swDelays[btn]); 
    }
}

void loop() {
    int currentButton = readButton();
    // ボタンが「押された瞬間」だけを検知（押しっぱなし防止）
    bool isPressed = (currentButton != -1 && lastButton == -1); 

    switch (currentState) {
        case TITLE:
            canvas.fillSprite(TFT_BLACK);
            canvas.drawCentreString("EGPWS SIMON", 64, 10);
            canvas.drawCentreString("Press Any Btn", 64, 25);
            
            // 4000以上の時（ボタンが押されておらず入力可能な時）に右上に●を描画
            if (currentRawADC >= 4000) {
                canvas.fillCircle(120, 8, 4, TFT_WHITE);
            }
            display.pushImage(0, 0, 128, 64, (uint16_t*)canvas.getBuffer());

            // 起動後5秒間は入力を完全に無視する（突入電流による勝手なスタートを回避）
            if (isPressed && millis() > 5000) {
                sequence.clear();
                sequence.push_back(random(0, 4)); // 最初の1音を追加
                
                playBuzzer(1); // スタートのブザー音を鳴らす

                currentState = PLAYING_SEQUENCE;
                stateChanged = true;
                delay(1000); // ゲーム開始前のタメ
            }
            break;

        case PLAYING_SEQUENCE:
            if (stateChanged) {
                canvas.fillSprite(TFT_BLACK);
                canvas.drawCentreString("WATCH & LISTEN", 64, 30);
                display.pushImage(0, 0, 128, 64, (uint16_t*)canvas.getBuffer());
                stateChanged = false;
            }

            // 記憶した順番通りにガジェットを鳴らす (false = お手本モードなので個別ディレイあり)
            for (int i = 0; i < sequence.size(); i++) {
                playTarget(sequence[i], false);
            }

            inputIndex = 0;
            currentState = WAITING_INPUT;
            stateChanged = true;
            break;

        case WAITING_INPUT:
            canvas.fillSprite(TFT_BLACK);
            canvas.drawCentreString("YOUR TURN!", 64, 10);
            canvas.drawCentreString("Level: " + String(sequence.size()), 64, 25);
            
            // 4000以上の時（ボタンが押されておらず入力可能な時）に右上に●を描画
            if (currentRawADC >= 4000) {
                canvas.fillCircle(120, 8, 4, TFT_WHITE);
            }
            display.pushImage(0, 0, 128, 64, (uint16_t*)canvas.getBuffer());

            if (isPressed) {
                int expected = sequence[inputIndex];
                
                if (currentButton == expected) {
                    // 正解：プレイヤーの入力に合わせてガジェットを鳴らす (true = プレイヤー入力なのでディレイなし)
                    playTarget(currentButton, true); 
                    inputIndex++;
                    
                    if (inputIndex >= sequence.size()) {
                        // 全て正解：次のレベルへ
                        sequence.push_back(random(0, 4));
                        currentState = PLAYING_SEQUENCE;
                        stateChanged = true;
                        delay(1000); // 次のレベルが始まる前のタメ
                    }
                } else {
                    // 不正解：ゲームオーバーへ遷移（音の処理はGAME_OVER内で実行）
                    currentState = GAME_OVER;
                    stateChanged = true;
                }
            }
            break;

        case GAME_OVER:
            if (stateChanged) {
                canvas.fillSprite(TFT_BLACK);
                canvas.drawCentreString("GAME OVER", 64, 15);
                canvas.drawCentreString("CRASH!!", 64, 35); // 墜落演出のテキストを追加
                display.pushImage(0, 0, 128, 64, (uint16_t*)canvas.getBuffer());
                stateChanged = false;
                
                // 画面にCRASH!!と表示させてから、長い警告ブザーを鳴らす
                playBuzzer(0); 
                
                delay(1000); // ブザーが鳴り終わった後、1秒だけ余韻を残してタイトルへ戻る
                currentState = TITLE;
                stateChanged = true;
            }
            break;
    }

    lastButton = currentButton;
    delay(50); // メインループの負荷軽減
}