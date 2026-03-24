export type PageView =
  | 'dashboard'
  | 'calendar'
  | 'nodes'
  | 'terminal'
  | 'grades'
  | 'alerts'
  | 'profile'
  | 'settings';

export interface NavItem {
  id: PageView;
  label: string;
  icon: string;
  badge?: boolean;
}