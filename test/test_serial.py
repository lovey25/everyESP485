#!/usr/bin/env python3
"""
RS485 시리얼 데이터 전송 테스트 스크립트
ESP8266의 대량 데이터 수신 안정성을 테스트합니다.
"""

import serial
import time
import sys

# 설정
PORT = '/dev/cu.usbserial-110'
BAUD = 74880
TEST_MODES = {
    '1': ('작은 패킷 연속 전송', b'\x01\x02\x03\x04\x05', 100, 0.05),
    '2': ('중간 패킷 연속 전송', b'\xAA\x55' * 50, 50, 0.1),
    '3': ('큰 패킷 연속 전송', b'\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09' * 100, 20, 0.2),
    '4': ('Modbus 시뮬레이션', b'\x01\x03\x00\x00\x00\x0A\xC5\xCD', 100, 0.1),
    '5': ('최대 버퍼 테스트 (8KB)', b'\xFF' * 8000, 5, 0.5),
}

def print_menu():
    print("\n=== RS485 시리얼 테스트 ===")
    print(f"포트: {PORT}, Baud: {BAUD}")
    print("\n테스트 모드 선택:")
    for key, (desc, data, count, delay) in TEST_MODES.items():
        print(f"  {key}. {desc} ({len(data)}바이트 x {count}회, {delay}초 간격)")
    print("  q. 종료")

def run_test(mode):
    desc, data, count, delay = TEST_MODES[mode]
    
    print(f"\n[{desc}] 시작...")
    print(f"데이터 크기: {len(data)} 바이트")
    print(f"전송 횟수: {count} 회")
    print(f"전송 간격: {delay} 초")
    print(f"총 전송량: {len(data) * count} 바이트")
    
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        time.sleep(0.5)  # 시리얼 포트 초기화 대기
        
        print("\n전송 시작 (Ctrl+C로 중단 가능)...")
        start_time = time.time()
        
        for i in range(count):
            ser.write(data)
            elapsed = time.time() - start_time
            print(f"[{i+1}/{count}] {len(data)} 바이트 전송 완료 (경과: {elapsed:.1f}초)", end='\r')
            sys.stdout.flush()
            time.sleep(delay)
        
        total_time = time.time() - start_time
        total_bytes = len(data) * count
        
        print(f"\n\n✓ 테스트 완료!")
        print(f"  총 전송: {total_bytes} 바이트")
        print(f"  소요 시간: {total_time:.2f} 초")
        print(f"  평균 속도: {total_bytes/total_time:.0f} 바이트/초")
        
        ser.close()
        
    except serial.SerialException as e:
        print(f"\n✗ 시리얼 포트 오류: {e}")
        print(f"  포트가 다른 프로그램에서 사용 중인지 확인하세요.")
        return False
    except KeyboardInterrupt:
        print(f"\n\n✗ 사용자가 테스트를 중단했습니다.")
        ser.close()
        return False
    except Exception as e:
        print(f"\n✗ 오류 발생: {e}")
        return False
    
    return True

def main():
    print("\n" + "="*50)
    print("ESP8266 RS485 시리얼 테스트 스크립트")
    print("="*50)
    
    # pyserial 설치 확인
    try:
        import serial
    except ImportError:
        print("\n✗ pyserial이 설치되지 않았습니다.")
        print("  설치: pip install pyserial")
        sys.exit(1)
    
    while True:
        print_menu()
        choice = input("\n선택: ").strip()
        
        if choice == 'q':
            print("종료합니다.")
            break
        
        if choice in TEST_MODES:
            run_test(choice)
            input("\nEnter를 눌러 계속...")
        else:
            print("잘못된 선택입니다.")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n프로그램을 종료합니다.")
        sys.exit(0)
