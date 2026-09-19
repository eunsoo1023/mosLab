# src/private

이 디렉터리는 학생이 자유롭게 private header와 helper C 파일을 추가하는 공간이다.

- 공개 API는 `include/minios/`를 변경하지 말고 구현한다.
- tests와 apps는 private 구조체나 함수 이름에 의존하지 않는다.
- Copilot에게 자료구조 선택 이유, 범위 검사, 오류 상태 전파를 함께 설명하도록 요청하라.