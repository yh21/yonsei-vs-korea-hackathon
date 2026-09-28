import argparse
import random
import subprocess
import sys


def parse_score(output_line):
  try:
    if "점수=" in output_line:
      part = output_line.split("점수=")[1].split()[0]
      return part
  except Exception:
    pass
  return ""


def main():
  parser = argparse.ArgumentParser(
      description="캠퍼스 정복전 멀티 벤치마크 러너"
  )
  parser.add_argument(
      "--botA",
      type=str,
      required=True,
      help="테스트 대상 봇 A 경로 (예: ./bot8)",
  )
  parser.add_argument(
      "--botB",
      type=str,
      required=True,
      help="상대 봇 B 경로 (예: ./bot6)",
  )
  # 기본값을 100으로 변경
  parser.add_argument(
      "--games", type=int, default=100, help="총 대전 판수 (기본값: 100)"
  )
  parser.add_argument(
      "--runner",
      type=str,
      default="yk-development-tools/bots/dist/starter/play_local.py",
      help="로컬 러너 경로",
  )
  args = parser.parse_args()

  total_games = args.games
  wins_a = 0
  wins_b = 0
  draws = 0

  print("=" * 60)
  print(f"🔥 대전 매칭: {args.botA}  VS  {args.botB}")
  print(f"🎮 총 경기 수: {total_games}판 (진영 교대 적용)")
  print("=" * 60)

  for i in range(1, total_games + 1):
    seed = random.randint(1, 1000000)

    # 홀수 판: A가 Y(신촌), B가 K(안암)
    # 짝수 판: B가 Y(신촌), A가 K(안암)
    if i % 2 != 0:
      side_a = "Y"
      side_b = "K"
      cmd = [
          "python3",
          args.runner,
          "--bot",
          args.botA,
          "--opponent",
          args.botB,
          "--seed",
          str(seed),
      ]
    else:
      side_a = "K"
      side_b = "Y"
      cmd = [
          "python3",
          args.runner,
          "--bot",
          args.botB,
          "--opponent",
          args.botA,
          "--seed",
          str(seed),
      ]

    res = subprocess.run(cmd, capture_output=True, text=True)
    out = res.stdout

    # 판정 분석
    winner = None
    if "승자=Y" in out:
      winner = "Y"
    elif "승자=K" in out:
      winner = "K"

    score_str = parse_score(out)

    if winner == side_a:
      wins_a += 1
      result_str = f"승리 ({args.botA})"
    elif winner == side_b:
      wins_b += 1
      result_str = f"패배 ({args.botB} 승)"
    else:
      draws += 1
      result_str = "무승부"

    print(
        f"[{i:03d}/{total_games}] Seed {seed:7d} | {args.botA}({side_a}) vs"
        f" {args.botB}({side_b}) -> {result_str} {score_str}"
    )

  win_rate_a = (wins_a / total_games) * 100
  win_rate_b = (wins_b / total_games) * 100

  print("\n" + "=" * 60)
  print("🏆 최종 벤치마크 결과 🏆")
  print(f"총 경기 수: {total_games}판")
  print(f"• {args.botA} 승리 : {wins_a}회 ({win_rate_a:.1f}%)")
  print(f"• {args.botB} 승리 : {wins_b}회 ({win_rate_b:.1f}%)")
  if draws > 0:
    print(f"• 무승부      : {draws}회 ({(draws/total_games)*100:.1f}%)")
  print("=" * 60)


if __name__ == "__main__":
  main()